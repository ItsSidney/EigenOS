/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/bus/pci.h"
#include "commands/commands.h"
#include "drivers/input/keyboard.h"

extern void draw_boot_log(void);

static pci_device_t pci_devices[64];
static int pci_count = 0;

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000);
    port_long_out(0xCF8, address);
    return port_long_in(0xCFC);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000);
    port_long_out(0xCF8, address);
    port_long_out(0xCFC, val);
}

extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void draw_boot_log(void);

void pci_init() {
    pci_count = 0;
//    boot_log_add("PCI", "Enumerating buses...", 0x58A6FF, 0xE8EAED);
//    draw_boot_log();
    for (int bus = 0; bus < 256; bus++) {
        if ((bus & 0xF) == 0) draw_boot_log();
        int has_device = 0;
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = pci_config_read(bus, slot, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;

            has_device = 1;

            uint32_t header = pci_config_read(bus, slot, 0, 0x0C);
            int is_multi = (header & 0x800000);

            for (int func = 0; func < (is_multi ? 8 : 1); func++) {
                id = pci_config_read(bus, slot, func, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                
                pci_device_t* dev = &pci_devices[pci_count++];
                dev->bus = bus;
                dev->slot = slot;
                dev->func = func;
                dev->vendor_id = id & 0xFFFF;
                dev->device_id = (id >> 16) & 0xFFFF;
                
                uint32_t class_info = pci_config_read(bus, slot, func, 0x08);
                dev->class_id = (class_info >> 24) & 0xFF;
                dev->subclass = (class_info >> 16) & 0xFF;
                dev->prog_if = (class_info >> 8) & 0xFF;
                
                uint32_t int_info = pci_config_read(bus, slot, func, 0x3C);
                dev->interrupt_line = int_info & 0xFF;

                for (int i = 0; i < 6; i++) {
                    uint32_t bar = pci_config_read(bus, slot, func, 0x10 + (i * 4));
                    dev->bar[i] = bar;
                    if ((bar & 0x6) == 0x4) {
                        uint64_t bar_high = pci_config_read(bus, slot, func, 0x10 + ((i + 1) * 4));
                        dev->bar_64[i] = ((uint64_t)bar_high << 32) | (bar & ~0xF);
                        i++;
                    } else {
                        dev->bar_64[i] = bar & ~0xF;
                    }
                }
                
                if (pci_count >= 64) return;
            }
        }
        if (!has_device && bus > 0) break;
    }
}

void pci_enable_bus_mastering(pci_device_t* dev) {
    uint32_t command = pci_config_read(dev->bus, dev->slot, dev->func, 0x04);
    command |= (1 << 2) | (1 << 1); // Bus Master | Memory Space
    pci_config_write(dev->bus, dev->slot, dev->func, 0x04, command);
}

int pci_get_device_count() { return pci_count; }
pci_device_t* pci_get_device(int index) { return (index < pci_count) ? &pci_devices[index] : 0; }

const char* pci_get_class_name(uint8_t class_id) {
    switch (class_id) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage Controller";
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x05: return "Memory Controller";
        case 0x06: return "Bridge Device";
        case 0x07: return "Simple Comm Controller";
        case 0x08: return "Base System Peripheral";
        case 0x09: return "Input Device Controller";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus Controller";
        case 0x0D: return "Wireless Controller";
        case 0x0E: return "Intelligent Controller";
        case 0x0F: return "Satellite Comm Controller";
        case 0x10: return "Encryption Controller";
        case 0x11: return "Signal Processing Controller";
        case 0x12: return "Processing Accelerator";
        case 0x13: return "Non-Essential Instrumentation";
        case 0x40: return "Co-Processor";
        default: return "Unknown";
    }
}

const char* pci_vendor_to_string(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x8086: return "Intel Corporation";
        case 0x10EC: return "Realtek Semiconductor Co., Ltd.";
        case 0x10DE: return "NVIDIA Corporation";
        case 0x1022: return "Advanced Micro Devices, Inc.";
        case 0x1002: return "Advanced Micro Devices, Inc. [AMD/ATI]";
        case 0x1234: return "QEMU Virtual Video Controller";
        case 0x80EE: return "Oracle Corporation - VirtualBox";
        case 0x15AD: return "VMware";
        case 0x1AF4: return "Red Hat, Inc. - Virtio";
        case 0x106B: return "Apple Inc.";
        case 0x14E4: return "Broadcom Inc. and subsidiaries";
        case 0x168C: return "Qualcomm Atheros";
        case 0x0A12: return "Cambridge Silicon Radio, Ltd.";
        case 0x0B05: return "ASUSTek Computer, Inc.";
        case 0x04CA: return "Lite-On Technology Corp.";
        case 0x105B: return "Foxconn International, Inc.";
        case 0x0E8D: return "MediaTek Inc.";
        case 0x13D3: return "AzureWave Technologies, Inc.";
        case 0x0489: return "Foxconn / Hon Hai Precision Ind.";
        case 0x0930: return "Toshiba Corp.";
        case 0x1B6F: return "Intel (Bluetooth PSF)";
        default: return "Unknown Vendor";
    }
}

const char* pci_device_to_string(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id == 0x8086) {
        switch (device_id) {
            case 0x100E: return "82540EM Gigabit Ethernet Controller";
            case 0x100F: return "82545EM Gigabit Ethernet Controller";
            case 0x101A: return "82546EB Gigabit Ethernet Controller";
            case 0x1570: return "Ethernet Connection I219-V";
            case 0x15F3: return "Ethernet Connection I225-V";
            case 0x24F3: return "Ethernet Controller I225-V";
            case 0x1237: return "440FX - 82441FX PMC [Natoma]";
            case 0x7000: return "82371SB PIIX3 ISA [Natoma/Triton II]";
            case 0x7010: return "82371SB PIIX3 IDE [Natoma/Triton II]";
            case 0x7110: return "82371AB/EB/MB PIIX4 ISA";
            case 0x7111: return "82371AB/EB/MB PIIX4 IDE";
            case 0x7113: return "82371AB/EB/MB PIIX4 ACPI";
            case 0x2415: return "82801AA AC'97 Audio Controller";
            case 0x2922: return "82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA Controller [AHCI mode]";
            case 0x2918: return "82801IB (ICH9) LPC Interface Controller";
            case 0x293e: return "82801I (ICH9 Family) HD Audio Controller";
            case 0x02F0: return "Wireless-AC 9560 [Jefferson Peak] Bluetooth";
            case 0x0A2B: return "WiFi/BT Combo (5xxx/6xxx series)";
            case 0x0A2C: return "WiFi/BT Combo (7xxx series)";
            case 0x0AAA: return "WiFi 6 AX200 Bluetooth";
            case 0x0AF0: return "WiFi 6 AX201 Bluetooth";
            case 0x2526: return "WiFi 6E AX211 Bluetooth";
            case 0x2726: return "WiFi 6E AX210 Bluetooth";
            case 0x43E0: return "500 Series Chipset Bluetooth";
            case 0xA0F0: return "WiFi/BT Combo CNVio2";
            case 0x9CBA: return "Wildcat Point-LP LPSS (BT)";
        }
    } else if (vendor_id == 0x1AF4) {
        switch (device_id) {
            case 0x1000: return "Virtio network device";
            case 0x1001: return "Virtio block device";
            case 0x1002: return "Virtio memory balloon";
            case 0x1003: return "Virtio console";
            case 0x1004: return "Virtio SCSI host bus adapter";
            case 0x1005: return "Virtio entropy source";
            case 0x1009: return "Virtio GPU";
            case 0x1010: return "Virtio input device";
            case 0x1011: return "Virtio balloon";
            case 0x1012: return "Virtio RNG";
            case 0x1013: return "Virtio SCSI";
            case 0x1014: return "Virtio 9P transport";
            case 0x1015: return "Virtio mac80211 wlan";
            case 0x1016: return "Virtio GPU context";
            case 0x1017: return "Virtio GPU context 2";
            case 0x1018: return "Virtio video decoder";
            case 0x1019: return "Virtio video encoder";
            case 0x101A: return "Virtio sound";
            case 0x101B: return "Virtio scmi";
            case 0x101C: return "Virtio pmem";
            case 0x101D: return "Virtio fs";
            case 0x101E: return "Virtio pci notify";
        }
    } else if (vendor_id == 0x10EC) {
        switch (device_id) {
            case 0x8139: return "RTL8139/RTL810x Family Fast Ethernet NIC";
            case 0x8168: return "RTL8111/8168/8411 PCI Express Gigabit Ethernet Controller";
            case 0x8169: return "RTL8169/8110 Gigabit Ethernet";
            case 0xB852: return "RTL8852AE WiFi/BT Combo";
            case 0xC822: return "RTL8822CE WiFi/BT Combo";
            case 0xB723: return "RTL8723BE WiFi/BT Combo";
            case 0x8723: return "RTL8723AE WiFi/BT Combo";
            case 0x8821: return "RTL8821AE WiFi/BT Combo";
        }
    } else if (vendor_id == 0x1234) {
        if (device_id == 0x1111) return "QEMU Virtual Video Controller";
    } else if (vendor_id == 0x14E4) {
        switch (device_id) {
            case 0x4335: return "BCM4335/BCM4345 Bluetooth + WiFi";
            case 0x43C3: return "BCM43C3 Wireless BT Combo";
            case 0x43EC: return "BCM43EC Bluetooth + WiFi";
            case 0x43E9: return "BCM43E9 Bluetooth";
            case 0x4464: return "BCM4464 Bluetooth";
        }
    } else if (vendor_id == 0x168C) {
        switch (device_id) {
            case 0x003E: return "Atheros AR9462 Bluetooth";
            case 0x0032: return "Atheros AR9485 WiFi + BT";
            case 0x0036: return "QCA9565/AR9565 WiFi/BT Combo";
        }
    } else if (vendor_id == 0x1022) {
        switch (device_id) {
            case 0x2000: return "AMD PCnet/PCI II (Am79C970A) Ethernet Controller";
            case 0x2001: return "AMD PCnet/FAST (Am79C971) Ethernet Controller";
        }
    } else if (vendor_id == 0x106B) {
        switch (device_id) {
            case 0x00F6: return "Broadcom BCM20703 Bluetooth (Apple)";
            case 0x010B: return "Bluetooth USB Host Controller (Apple)";
        }
    }
    return "Unknown Device";
}
