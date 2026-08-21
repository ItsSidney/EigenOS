/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/arch/x86_64/tss.h"
#include "kernel/arch/x86_64/gdt.h"

tss_t tss;

void init_tss() {
    // Clear TSS
    for (unsigned int i = 0; i < sizeof(tss_t); i++) {
        ((char*)&tss)[i] = 0;
    }
    
    // Set up Ring 0 stack (kernel stack) - will be updated per task
    tss.rsp0 = 0x90000;
    
    // Set I/O map base to end of TSS (no I/O permission map)
    tss.iomap_base = sizeof(tss_t);
}

void set_tss_stack(uint64_t stack0) {
    tss.rsp0 = stack0;
}

// Called when switching to a user task - update TSS with that task's kernel stack
void tss_set_user_rsp0(uint64_t kernel_stack_top) {
    tss.rsp0 = kernel_stack_top;
}