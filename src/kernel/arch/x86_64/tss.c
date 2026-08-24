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

    /* Interrupt Stack Table: #DF(IST1) NMI(IST2) MC(IST3).
     * Static so they exist before the heap is up. A stack-switched fault
     * (e.g. kernel-stack overflow) lands here instead of triple-faulting. */
    {
        static __attribute__((aligned(16))) uint8_t ist_stack[3][16384];
        tss.ist1 = (uint64_t)(ist_stack[0] + sizeof(ist_stack[0]));
        tss.ist2 = (uint64_t)(ist_stack[1] + sizeof(ist_stack[1]));
        tss.ist3 = (uint64_t)(ist_stack[2] + sizeof(ist_stack[2]));
    }
}

void set_tss_stack(uint64_t stack0) {
    tss.rsp0 = stack0;
}

// Called when switching to a user task - update TSS with that task's kernel stack
void tss_set_user_rsp0(uint64_t kernel_stack_top) {
    tss.rsp0 = kernel_stack_top;
}