/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_SIZE (sizeof(idt_entry_t) * IDT_ENTRIES)

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

void init_idt();
void set_idt_entry(int num, uint64_t base, uint16_t sel, uint8_t flags);

#endif
