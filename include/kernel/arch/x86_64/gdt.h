/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Installs the Eigen GDT with Ring 3 user segments and loads the TSS.
// Must be called once during early boot (before kmain).
void install_gdt(void);

// Per-CPU data for syscall entry/exit
struct cpu_data {
    uint64_t user_rsp;    // Saved user RSP during syscall
    uint64_t kernel_rsp;  // Kernel stack pointer for this CPU
};

extern struct cpu_data cpu_data;

// MSR operations
void wrmsr(uint32_t msr, uint64_t val);

void install_gdt(void);
void init_syscall_gs_base(void);

// Segment selectors (for reference)
#define SEG_KERNEL_CODE  0x08
#define SEG_KERNEL_DATA  0x10
#define SEG_USER_CODE    0x1B   // 0x18 | RPL 3
#define SEG_USER_DATA    0x23   // 0x20 | RPL 3
#define SEG_TSS          0x28

#endif