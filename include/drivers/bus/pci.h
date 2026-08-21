/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t interrupt_line;
    uint32_t bar[6];
    uint64_t bar_64[6];
} pci_device_t;

void pci_init();
void pci_enable_bus_mastering(pci_device_t* dev);
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
int pci_get_device_count();
pci_device_t* pci_get_device(int index);
const char* pci_get_class_name(uint8_t class_id);
const char* pci_vendor_to_string(uint16_t vendor_id);
const char* pci_device_to_string(uint16_t vendor_id, uint16_t device_id);

#endif
