/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef TSS_H
#define TSS_H

#include <stdint.h>

// 64-bit Task State Segment structure
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;        // Stack pointer for Ring 0
    uint64_t rsp1;        // Stack pointer for Ring 1
    uint64_t rsp2;        // Stack pointer for Ring 2
    uint64_t reserved1;
    uint64_t ist1;        // Interrupt Stack Table entries
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;  // I/O map base address
} __attribute__((packed)) tss_t;

// TSS functions
void init_tss();
void set_tss_stack(uint64_t stack0);
void tss_set_user_rsp0(uint64_t kernel_stack_top);

#endif
