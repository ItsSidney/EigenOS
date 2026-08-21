/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

#define ACPI_SIG(a,b,c,d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define ACPI_RSDP_SIG    0x2052545020445352ULL  // "RSD PTR "
#define ACPI_RSDT_SIG    ACPI_SIG('R','S','D','T')
#define ACPI_XSDT_SIG    ACPI_SIG('X','S','D','T')
#define ACPI_FADT_SIG    ACPI_SIG('F','A','C','P')
#define ACPI_DSDT_SIG    ACPI_SIG('D','S','D','T')
#define ACPI_FACS_SIG    ACPI_SIG('F','A','C','S')
#define ACPI_APIC_SIG    ACPI_SIG('A','P','I','C')
#define ACPI_HPET_SIG    ACPI_SIG('H','P','E','T')
#define ACPI_MCFG_SIG    ACPI_SIG('M','C','F','G')
#define ACPI_SSDT_SIG    ACPI_SIG('S','S','D','T')
#define ACPI_SBAT_SIG    ACPI_SIG('S','B','A','T')

struct acpi_rsdp {
    uint64_t signature;
    uint8_t  checksum;
    uint8_t  oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    uint32_t signature;
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    uint8_t  oem_id[6];
    uint8_t  oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_fadt {
    struct acpi_sdt_header header;
    uint32_t facs;
    uint32_t dsdt;
    uint8_t  int_model;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved;
    uint32_t flags;
    struct {
        uint8_t  address_space_id;
        uint8_t  register_bit_width;
        uint8_t  register_bit_offset;
        uint8_t  reserved;
        uint64_t address;
    } reset_reg;
    uint8_t  reset_value;
    uint8_t  reserved2[3];
    uint64_t x_facs;
    uint64_t x_dsdt;
    struct {
        uint8_t  address_space_id;
        uint8_t  register_bit_width;
        uint8_t  register_bit_offset;
        uint8_t  reserved;
        uint64_t address;
    } x_pm1a_evt_blk, x_pm1b_evt_blk, x_pm1a_cnt_blk, x_pm1b_cnt_blk, x_pm2_cnt_blk, x_pm_tmr_blk, x_gpe0_blk, x_gpe1_blk;
} __attribute__((packed));

// MADT/APIC structures
struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_addr;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

#define MADT_TYPE_LOCAL_APIC      0
#define MADT_TYPE_IO_APIC         1
#define MADT_TYPE_ISO             2
#define MADT_TYPE_NMI             3
#define MADT_TYPE_LOCAL_APIC_OVERRIDE 5

struct acpi_madt_local_apic {
    struct acpi_madt_entry_header header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_io_apic {
    struct acpi_madt_entry_header header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_iso {
    struct acpi_madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

// HPET
struct acpi_hpet {
    struct acpi_sdt_header header;
    uint8_t  hardware_rev_id;
    uint8_t  comparator_count : 5;
    uint8_t  counter_size : 1;
    uint8_t  reserved : 1;
    uint8_t  legacy_replacement : 1;
    uint16_t pci_vendor_id;
    struct {
        uint8_t  address_space_id;
        uint8_t  register_bit_width;
        uint8_t  register_bit_offset;
        uint8_t  reserved;
        uint64_t address;
    } base_address;
    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} __attribute__((packed));

// MCFG (PCIe)
struct acpi_mcfg {
    struct acpi_sdt_header header;
    uint64_t reserved;
} __attribute__((packed));

struct acpi_mcfg_entry {
    uint64_t base_addr;
    uint16_t pci_seg_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} __attribute__((packed));

// FACS
struct acpi_facs {
    uint32_t signature;
    uint32_t length;
    uint32_t hardware_signature;
    uint32_t waking_vector;
    uint32_t global_lock;
    uint32_t flags;
    uint64_t x_waking_vector;
    uint8_t  version;
    uint8_t  reserved[3];
    uint32_t ospm_flags;
    uint8_t  reserved2[24];
} __attribute__((packed));

struct acpi_battery_info {
    char  unit_name[16];
    uint32_t design_capacity;
    uint32_t last_full_capacity;
    uint32_t technology;
    uint32_t design_voltage;
    uint32_t design_capacity_warning;
    uint32_t design_capacity_low;
    uint16_t cycle_count;
    uint16_t accuracy;
    uint32_t max_sampling_time;
    uint32_t min_sampling_time;
    char  model_number[16];
    char  serial_number[16];
    char  battery_type[16];
    char  oem_info[16];
};

struct acpi_battery_status {
    uint32_t state;
    uint32_t present_rate;
    uint32_t remaining_capacity;
    uint32_t present_voltage;
};

int acpi_init(void);
struct acpi_rsdp* acpi_find_rsdp(void);
struct acpi_fadt* acpi_find_fadt(void);
struct acpi_sdt_header* acpi_find_table(const char* signature);
void* acpi_find_table_by_sig(uint32_t signature);
int acpi_get_battery_info(struct acpi_battery_info* info);
int acpi_get_battery_status(struct acpi_battery_status* status);

// Extended ACPI info accessors
const char* acpi_get_oem_id(void);
const char* acpi_get_oem_table_id(void);
int acpi_has_apic(void);
int acpi_has_hpet(void);
int acpi_has_mcfg(void);
uint32_t acpi_get_io_apic_count(void);
uint32_t acpi_get_io_apic_addr(int index);
int acpi_pm_timer_available(void);
void acpi_reboot(void);
void acpi_power_off(void);

#endif