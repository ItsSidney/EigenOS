/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/arch/x86_64/tss.h"

extern tss_t tss;

//extern void serial_puts(const char*);

#include <stdint.h>

// CPUID function
static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile ("cpuid" 
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

// Check if MSR is supported (CPUID leaf 1, EDX bit 5)
static inline int msr_supported(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (edx & (1 << 5)) != 0;  // EDX bit 5 = MSR
}

// Write MSR
void wrmsr(uint32_t msr, uint64_t val) {
    if (msr_supported()) {
        __asm__ volatile ("wrmsr" : : "c"(msr), "a"(val), "d"(val >> 32));
    }
}

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

// Per-CPU data for syscall entry/exit
struct cpu_data {
    uint64_t user_rsp;    // Saved user RSP during syscall
    uint64_t kernel_rsp;  // Kernel stack pointer for this CPU
} cpu_data;

gdt_entry_t gdt[7];  // 7 entries: 0=null, 1=kernel code, 2=kernel data, 3=user code, 4=user data, 5=low TSS, 6=high TSS
gdt_ptr_t gdt_ptr;

void set_gdt_entry(int num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

void set_tss_entry(int num, uint64_t base, uint32_t limit) {
    // A 64-bit TSS descriptor spans TWO GDT entries (16 bytes).
    //   bytes 0-1: limit low
    //   bytes 2-3: base low
    //   byte  4:   base middle
    //   byte  5:   access
    //   byte  6:   limit high (4 bits) + flags
    //   byte  7:   base high
    //   bytes 8-11: base upper 32   <-- must be at OFFSET 8, not in the
    //                                   fields of the second entry!
    //   bytes 12-15: reserved
    uint8_t* d = (uint8_t*)&gdt[num];
    d[0] = (limit >> 0) & 0xFF;
    d[1] = (limit >> 8) & 0xFF;
    d[2] = (base >> 0) & 0xFF;
    d[3] = (base >> 8) & 0xFF;
    d[4] = (base >> 16) & 0xFF;
    d[5] = 0x89;  // Present, DPL=0, Type=9 (64-bit TSS available)
    d[6] = ((limit >> 16) & 0x0F) | 0x00;
    d[7] = (base >> 24) & 0xFF;
    d[8] = (base >> 32) & 0xFF;
    d[9] = (base >> 40) & 0xFF;
    d[10] = (base >> 48) & 0xFF;
    d[11] = (base >> 56) & 0xFF;
    d[12] = 0;
    d[13] = 0;
    d[14] = 0;
    d[15] = 0;
}

extern void init_tss();
extern void load_tss();

void install_gdt(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 7) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    set_gdt_entry(0, 0, 0, 0, 0);                // Null segment
    set_gdt_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // Kernel code (64-bit)
    set_gdt_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel data
    set_gdt_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xAF); // User code (64-bit)
    set_gdt_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User data
    
    // TSS uses TWO entries (5 and 6) for 64-bit base address
    set_tss_entry(5, (uint64_t)&tss, sizeof(tss_t));

    extern void gdt_flush(uint64_t);
    gdt_flush((uint64_t)&gdt_ptr);
}

// Called after tasking is initialized to set up TSS and per-CPU area
void init_syscall_gs_base(void) {
//    serial_puts("[EIGEN] init_syscall_gs_base: init_tss\n");
    // Initialize TSS
    init_tss();
//    serial_puts("[EIGEN] init_syscall_gs_base: load_tss\n");
    // Load TSS via assembly function
    load_tss();
//    serial_puts("[EIGEN] init_syscall_gs_base: wrmsr\n");
    // Set up GS base for per-CPU area (for swapgs in syscall handler)
    // GS base is stored in IA32_GS_BASE MSR (0xC0000101)
    wrmsr(0xC0000101, (uint64_t)&cpu_data);
//    serial_puts("[EIGEN] init_syscall_gs_base: done\n");
}