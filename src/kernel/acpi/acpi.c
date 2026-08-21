/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/acpi.h"
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include "kernel/log.h"
#include "limine.h"
#include <string.h>

extern uint64_t hhdm_offset;
extern volatile struct limine_rsdp_request rsdp_request;

static struct acpi_rsdp* g_rsdp = NULL;
static struct acpi_fadt* g_fadt = NULL;
static struct acpi_sdt_header* g_xsdt = NULL;
static struct acpi_sdt_header* g_dsdt = NULL;
static struct acpi_facs* g_facs = NULL;
static struct acpi_madt* g_madt = NULL;
static struct acpi_hpet* g_hpet = NULL;
static struct acpi_mcfg* g_mcfg = NULL;

static int g_apic_found = 0;
static int g_hpet_found = 0;
static int g_mcfg_found = 0;
static int g_pm_timer_avail = 0;

#define MAX_IO_APICS 16
static uint32_t g_io_apic_addrs[MAX_IO_APICS];
static int g_io_apic_count = 0;

static char g_oem_id[7] = {0};
static char g_oem_table_id[9] = {0};

static int acpi_verify_checksum(void* ptr, uint32_t len) {
    uint8_t* p = (uint8_t*)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += p[i];
    return sum == 0;
}

static int acpi_sdt_valid(struct acpi_sdt_header* hdr) {
    if (!hdr) return 0;
    if (hdr->length < sizeof(struct acpi_sdt_header)) return 0;
    if (hdr->length > 0x100000) return 0;
    return acpi_verify_checksum(hdr, hdr->length);
}

static void* hhdm_ptr(uint64_t phys) {
    if (phys == 0) return NULL;
    if (phys >= hhdm_offset) return (void*)phys;
    return (void*)(phys + hhdm_offset);
}

struct acpi_rsdp* acpi_find_rsdp(void) {
    if (g_rsdp) return g_rsdp;

    if (!rsdp_request.response || !rsdp_request.response->address) {
        klog("[ACPI] No RSDP from bootloader\n");
        return NULL;
    }

    struct acpi_rsdp* rsdp = (struct acpi_rsdp*)hhdm_ptr((uint64_t)rsdp_request.response->address);
    if (!rsdp) {
        klog("[ACPI] RSDP address invalid\n");
        return NULL;
    }

    if (rsdp->signature != ACPI_RSDP_SIG) {
        klog("[ACPI] RSDP sig mismatch\n");
        return NULL;
    }

    int checksum_ok = 0;
    if (rsdp->revision >= 2) {
        checksum_ok = acpi_verify_checksum(rsdp, sizeof(struct acpi_rsdp));
    }
    if (!checksum_ok) {
        checksum_ok = acpi_verify_checksum(rsdp, 20);
    }
    if (!checksum_ok) {
        klog("[ACPI] RSDP chksum fail\n");
        return NULL;
    }

    g_rsdp = rsdp;

    for (int i = 0; i < 6 && rsdp->oem_id[i]; i++) g_oem_id[i] = rsdp->oem_id[i];
    g_oem_id[6] = 0;

    klog("[ACPI] RSDP found rev=");
    klog(g_rsdp->revision >= 2 ? "2" : "1");
    klog("\n");
    return g_rsdp;
}

static void parse_madt(void) {
    if (!g_madt) return;

    uint8_t* ptr = (uint8_t*)(g_madt + 1);
    uint8_t* end = (uint8_t*)g_madt + g_madt->header.length;

    while (ptr + sizeof(struct acpi_madt_entry_header) <= end) {
        struct acpi_madt_entry_header* entry = (struct acpi_madt_entry_header*)ptr;
        if (entry->length < sizeof(struct acpi_madt_entry_header)) break;
        if (ptr + entry->length > end) break;

        switch (entry->type) {
            case MADT_TYPE_IO_APIC: {
                struct acpi_madt_io_apic* ioapic = (struct acpi_madt_io_apic*)entry;
                if (g_io_apic_count < MAX_IO_APICS) {
                    g_io_apic_addrs[g_io_apic_count++] = ioapic->io_apic_addr;
                }
                break;
            }
            default:
                break;
        }
        ptr += entry->length;
    }

    if (g_io_apic_count > 0) {
        klog("[ACPI] MADT: IO APICs found\n");
    }
}

struct acpi_sdt_header* acpi_find_table(const char* signature) {
    uint32_t sig_val = ACPI_SIG(signature[0], signature[1], signature[2], signature[3]);
    return (struct acpi_sdt_header*)acpi_find_table_by_sig(sig_val);
}

void* acpi_find_table_by_sig(uint32_t signature) {
    if (!acpi_find_rsdp()) return NULL;

    int use_xsdt = (g_rsdp->revision >= 2 && g_rsdp->xsdt_address != 0);

    if (use_xsdt) {
        uint64_t xsdt_phys = g_rsdp->xsdt_address;
        uint64_t xsdt_virt = xsdt_phys + hhdm_offset;
        if (!vmm_virt_mapped(xsdt_virt)) {
            klog("[ACPI] XSDT addr not mapped\n");
            use_xsdt = 0;
        } else {
            g_xsdt = (struct acpi_sdt_header*)xsdt_virt;
            if (!acpi_sdt_valid(g_xsdt)) {
                klog("[ACPI] XSDT invalid\n");
                use_xsdt = 0;
            }
        }
    }

    if (!use_xsdt && g_rsdp->rsdt_address) {
        uint64_t rsdt_phys = g_rsdp->rsdt_address;
        uint64_t rsdt_virt = rsdt_phys + hhdm_offset;
        if (!vmm_virt_mapped(rsdt_virt)) {
            klog("[ACPI] RSDT addr not mapped\n");
            return NULL;
        }
        g_xsdt = (struct acpi_sdt_header*)rsdt_virt;
        if (!acpi_sdt_valid(g_xsdt)) {
            klog("[ACPI] RSDT invalid\n");
            return NULL;
        }
        use_xsdt = 0;
    }

    if (!g_xsdt) return NULL;

    if (use_xsdt) {
        uint64_t* entries = (uint64_t*)(g_xsdt + 1);
        int count = (g_xsdt->length - sizeof(struct acpi_sdt_header)) / 8;
        for (int i = 0; i < count; i++) {
            uint64_t tbl_virt = entries[i] + hhdm_offset;
            if (!vmm_virt_mapped(tbl_virt)) continue;
            struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)tbl_virt;
            if (hdr->signature == signature && acpi_sdt_valid(hdr)) {
                return (void*)hdr;
            }
        }
    } else {
        uint32_t* entries = (uint32_t*)(g_xsdt + 1);
        int count = (g_xsdt->length - sizeof(struct acpi_sdt_header)) / 4;
        for (int i = 0; i < count; i++) {
            uint64_t tbl_virt = (uint64_t)entries[i] + hhdm_offset;
            if (!vmm_virt_mapped(tbl_virt)) continue;
            struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)tbl_virt;
            if (hdr->signature == signature && acpi_sdt_valid(hdr)) {
                return (void*)hdr;
            }
        }
    }

    return NULL;
}

struct acpi_fadt* acpi_find_fadt(void) {
    if (g_fadt) return g_fadt;
    g_fadt = (struct acpi_fadt*)acpi_find_table_by_sig(ACPI_FADT_SIG);
    if (g_fadt) {
        klog("[ACPI] FADT found\n");
    }
    return g_fadt;
}

static int acpi_parse_fadt(void) {
    if (!acpi_find_fadt()) return 0;

    uint64_t dsdt_addr = 0;
    if (g_fadt->header.revision >= 2 && g_fadt->x_dsdt) {
        dsdt_addr = g_fadt->x_dsdt;
    } else if (g_fadt->dsdt) {
        dsdt_addr = g_fadt->dsdt;
    }

    if (dsdt_addr) {
        uint64_t dsdt_virt = dsdt_addr + hhdm_offset;
        if (vmm_virt_mapped(dsdt_virt)) {
            g_dsdt = (struct acpi_sdt_header*)dsdt_virt;
            if (g_dsdt->signature == ACPI_DSDT_SIG && acpi_sdt_valid(g_dsdt)) {
                klog("[ACPI] DSDT found\n");
            } else {
                g_dsdt = NULL;
                klog("[ACPI] DSDT invalid\n");
            }
        } else {
            klog("[ACPI] DSDT not mapped\n");
        }
    }

    uint64_t facs_addr = 0;
    if (g_fadt->header.revision >= 2 && g_fadt->x_facs) {
        facs_addr = g_fadt->x_facs;
    } else if (g_fadt->facs) {
        facs_addr = g_fadt->facs;
    }

    if (facs_addr) {
        uint64_t facs_virt = facs_addr + hhdm_offset;
        if (vmm_virt_mapped(facs_virt)) {
            g_facs = (struct acpi_facs*)facs_virt;
            if (g_facs->signature == ACPI_FACS_SIG) {
                klog("[ACPI] FACS found\n");
            } else {
                g_facs = NULL;
            }
        }
    }

    if (g_fadt->pm_tmr_blk && g_fadt->pm_tmr_len >= 4) {
        g_pm_timer_avail = 1;
        klog("[ACPI] PM timer available\n");
    }

    return 1;
}

static int acpi_parse_ssdts(void) {
    if (!g_rsdp) return 0;

    int use_xsdt = (g_rsdp->revision >= 2 && g_rsdp->xsdt_address != 0);
    int ssdt_count = 0;

    if (use_xsdt && g_xsdt) {
        uint64_t* entries = (uint64_t*)(g_xsdt + 1);
        int count = (g_xsdt->length - sizeof(struct acpi_sdt_header)) / 8;
        for (int i = 0; i < count; i++) {
            uint64_t tbl_virt = entries[i] + hhdm_offset;
            if (!vmm_virt_mapped(tbl_virt)) continue;
            struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)tbl_virt;
            if (hdr->signature == ACPI_SSDT_SIG && acpi_sdt_valid(hdr)) {
                ssdt_count++;
            }
        }
    } else if (g_xsdt) {
        uint32_t* entries = (uint32_t*)(g_xsdt + 1);
        int count = (g_xsdt->length - sizeof(struct acpi_sdt_header)) / 4;
        for (int i = 0; i < count; i++) {
            uint64_t tbl_virt = (uint64_t)entries[i] + hhdm_offset;
            if (!vmm_virt_mapped(tbl_virt)) continue;
            struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)tbl_virt;
            if (hdr->signature == ACPI_SSDT_SIG && acpi_sdt_valid(hdr)) {
                ssdt_count++;
            }
        }
    }

    if (ssdt_count > 0) {
        klog("[ACPI] SSDTs found\n");
    }

    return 1;
}

int acpi_has_apic(void) { return g_apic_found; }
int acpi_has_hpet(void) { return g_hpet_found; }
int acpi_has_mcfg(void) { return g_mcfg_found; }

const char* acpi_get_oem_id(void) { return g_oem_id; }
const char* acpi_get_oem_table_id(void) { return g_oem_table_id; }

uint32_t acpi_get_io_apic_count(void) { return g_io_apic_count; }

uint32_t acpi_get_io_apic_addr(int index) {
    if (index < 0 || index >= g_io_apic_count) return 0;
    return g_io_apic_addrs[index];
}

int acpi_pm_timer_available(void) { return g_pm_timer_avail; }

void acpi_reboot(void) {
    if (!acpi_find_fadt()) return;

    uint16_t reset_port = 0;
    uint8_t reset_val = g_fadt->reset_value;

    if (g_fadt->reset_reg.address_space_id == 1) {
        reset_port = (uint16_t)(g_fadt->reset_reg.address & 0xFFFF);
    }

    if (reset_port) {
        __asm__ volatile ("outb %0, %1" : : "a"(reset_val), "Nd"(reset_port));
    } else {
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    }
}

void acpi_power_off(void) {
    if (acpi_find_fadt()) {
        uint16_t pm1a_cnt = g_fadt->pm1a_cnt_blk;
        if (pm1a_cnt && g_fadt->pm1_cnt_len >= 2) {
            uint16_t slp_en = (1 << 13);
            __asm__ volatile ("outw %0, %1" : : "a"(slp_en), "Nd"(pm1a_cnt));
        }
    }
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
}

int acpi_get_battery_info(struct acpi_battery_info* info) {
    if (!info) return -1;
    memset(info, 0, sizeof(struct acpi_battery_info));

    strcpy(info->unit_name, "BAT0");
    info->design_capacity = 5000;
    info->last_full_capacity = 4500;
    info->technology = 2;
    info->design_voltage = 11400;
    info->design_capacity_warning = 250;
    info->design_capacity_low = 150;
    info->cycle_count = 0;
    info->accuracy = 1000;
    info->max_sampling_time = 60;
    info->min_sampling_time = 10;
    strcpy(info->model_number, "ACPI Battery");
    strcpy(info->serial_number, "Unknown");
    strcpy(info->battery_type, "Li-ion");
    strcpy(info->oem_info, "ACPI");
    return 0;
}

int acpi_get_battery_status(struct acpi_battery_status* status) {
    if (!status) return -1;
    memset(status, 0, sizeof(struct acpi_battery_status));
    status->state = 2;
    status->present_rate = 500;
    status->remaining_capacity = 3000;
    status->present_voltage = 11200;
    return 0;
}

int acpi_init(void) {
    klog("[ACPI] Init start\n");

    if (!acpi_find_rsdp()) {
        klog("[ACPI] No RSDP\n");
        return 0;
    }

    klog("[ACPI] After RSDP\n");

    acpi_parse_fadt();

    klog("[ACPI] After FADT\n");

    g_madt = (struct acpi_madt*)acpi_find_table_by_sig(ACPI_APIC_SIG);
    if (g_madt) {
        g_apic_found = 1;
        klog("[ACPI] MADT found, parsing...\n");
        parse_madt();
        klog("[ACPI] MADT done\n");
    } else {
        klog("[ACPI] No APIC table\n");
    }

    g_hpet = (struct acpi_hpet*)acpi_find_table_by_sig(ACPI_HPET_SIG);
    if (g_hpet) {
        g_hpet_found = 1;
        klog("[ACPI] HPET found\n");
    } else {
        klog("[ACPI] No HPET table\n");
    }

    g_mcfg = (struct acpi_mcfg*)acpi_find_table_by_sig(ACPI_MCFG_SIG);
    if (g_mcfg) {
        g_mcfg_found = 1;
        klog("[ACPI] MCFG found\n");
    } else {
        klog("[ACPI] No MCFG table\n");
    }

    acpi_parse_ssdts();

    klog("[ACPI] Init done\n");
    return 0;
}
