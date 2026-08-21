/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/mem/vmm.h"
#include "limine.h"
#include "drivers/video/framebuffer.h"

extern uint64_t hhdm_offset;

static uint64_t* current_pml4;

uint64_t vmm_kernel_pml4_phys = 0;

static inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline uint64_t* active_pml4(void) {
    return (uint64_t*)(read_cr3() + hhdm_offset);
}

static inline void invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

//extern void serial_puts(const char* s);

void vmm_init(void) {
//    serial_puts("[VMM] Initializing...\n");
    pmm_init();
    current_pml4 = (uint64_t*)(read_cr3() + hhdm_offset);
    vmm_kernel_pml4_phys = read_cr3();
//    serial_puts("[VMM] Initialized\n");
}

uint64_t vmm_get_current_pml4(void) {
    return read_cr3();
}

// Deep-copy the user half of the address space; share the kernel half.
// A shallow PML4 copy would leave the child sharing the parent's PDPT/PD/PT
// tables, so ELF/stack/heap mappings of one process would clobber the other.
// Huge-page entries map shared physical memory, so they can be shared as-is;
// only table pointers (non-huge entries) need fresh copies.
static uint64_t* clone_table(uint64_t* src, int level) {
    uint64_t phys = pmm_alloc();
    if (phys == 0) return NULL;
    uint64_t* dst = (uint64_t*)(phys + hhdm_offset);
    for (int j = 0; j < 512; j++) dst[j] = 0;
    for (int j = 0; j < 512; j++) {
        uint64_t e = src[j];
        if (!(e & VMM_PRESENT)) continue;
        /* PT level (or a huge page): shared physical memory, copy entry. */
        if (level >= 2 || (e & VMM_HUGE)) { dst[j] = e; continue; }
        uint64_t* child = clone_table(
            (uint64_t*)((e & 0x000FFFFFFFFFF000ULL) + hhdm_offset),
            level + 1);
        if (!child) return NULL;
        dst[j] = ((uint64_t)child - hhdm_offset) | (e & 0xFFF);
    }
    return dst;
}

uint64_t vmm_clone_current_pml4(void) {
    uint64_t new_phys = pmm_alloc();
    if (!new_phys) return 0;
    uint64_t* src = active_pml4();
    uint64_t* dst = (uint64_t*)(new_phys + hhdm_offset);
    for (int i = 0; i < 512; i++) dst[i] = 0;
    for (int i = 0; i < 512; i++) {
        uint64_t e = src[i];
        if (!(e & VMM_PRESENT)) continue;
        if (i >= 256) { dst[i] = e; continue; } /* kernel half: share */
        uint64_t* pdpt = clone_table(
            (uint64_t*)((e & 0x000FFFFFFFFFF000ULL) + hhdm_offset), 0);
        if (!pdpt) return 0;
        dst[i] = ((uint64_t)pdpt - hhdm_offset) | (e & 0xFFF);
    }
    return new_phys;
}

void vmm_activate_pml4(uint64_t pml4_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

/* Allocate a FRESH user address space for a new ring-3 process: share the
   kernel half (i>=256) and leave the user half (i<256) empty. The ELF loader
   and user-stack setup map their own pages on demand via vmm_map_range, so no
   inherited user mappings are needed. This is O(512) instead of the deep
   recursive clone of vmm_clone_current_pml4() — which copied every boot
   module / framebuffer mapping and could take seconds with IRQs disabled
   during spawn (5s input freeze on app launch). */
uint64_t vmm_new_user_pml4(void) {
    uint64_t new_phys = pmm_alloc();
    if (!new_phys) return 0;
    uint64_t* src = active_pml4();
    uint64_t* dst = (uint64_t*)(new_phys + hhdm_offset);
    for (int i = 0; i < 512; i++) {
        dst[i] = (i >= 256) ? src[i] : 0;   /* share kernel half, empty user half */
    }
    return new_phys;
}

static uint64_t* get_next_level(uint64_t* table, uint64_t index, bool allocate) {
    if (table[index] & VMM_PRESENT) {
        return (uint64_t*)((table[index] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    }
    if (!allocate) return NULL;

    uint64_t phys = pmm_alloc();
    if (phys == 0) return NULL;

    uint64_t* virt = (uint64_t*)(phys + hhdm_offset);
    for (int i = 0; i < 512; i++) virt[i] = 0;

    table[index] = phys | VMM_PRESENT | VMM_WRITE | VMM_USER;
    return virt;
}

// Split a 2MB huge page (PD entry) into a page table of 4K entries so a
// fine-grained mapping can overwrite just part of it. The other 511 pages
// keep the original huge page's physical target and flags (minus PS).
// USER is propagated only when the new mapping needs ring-3 access.
static uint64_t* split_huge_2m(uint64_t* pd, uint64_t idx, uint64_t new_flags) {
    uint64_t old = pd[idx];
    uint64_t base = old & 0x000FFFFFFFE00000ULL;
    uint64_t flags = (old & 0xFFF) & ~VMM_HUGE;

    uint64_t pt_phys = pmm_alloc();
    if (pt_phys == 0) return NULL;
    uint64_t* pt = (uint64_t*)(pt_phys + hhdm_offset);

    for (int j = 0; j < 512; j++) {
        pt[j] = (base + (uint64_t)j * 4096) | flags;
    }

    uint64_t pd_flags = VMM_PRESENT | VMM_WRITE;
    if (new_flags & VMM_USER) pd_flags |= VMM_USER;
    pd[idx] = pt_phys | pd_flags;
    return pt;
}

// Split a 1GB huge page (PDPT entry) into 512 2MB huge pages so a mapping can
// later split the specific 2MB chunk it needs.
static uint64_t* split_huge_1g(uint64_t* pdpt, uint64_t idx, uint64_t new_flags) {
    uint64_t old = pdpt[idx];
    uint64_t base = old & 0x000FFFFFC0000000ULL;
    uint64_t flags = ((old & 0xFFF) & ~VMM_HUGE) | VMM_HUGE;

    uint64_t pd_phys = pmm_alloc();
    if (pd_phys == 0) return NULL;
    uint64_t* pd = (uint64_t*)(pd_phys + hhdm_offset);

    for (int j = 0; j < 512; j++) {
        pd[j] = (base + (uint64_t)j * 0x200000) | flags;
    }

    uint64_t pdpt_flags = VMM_PRESENT | VMM_WRITE;
    if (new_flags & VMM_USER) pdpt_flags |= VMM_USER;
    pdpt[idx] = pd_phys | pdpt_flags;
    return pd;
}

bool vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx = (virt >> 21) & 0x1FF;
    uint64_t pt_idx = (virt >> 12) & 0x1FF;

    uint64_t* pml4 = active_pml4();
    uint64_t* pdpt = get_next_level(pml4, pml4_idx, true);
    if (!pdpt) return false;
    if (pdpt[pdpt_idx] & VMM_HUGE) {
        if (!split_huge_1g(pdpt, pdpt_idx, flags)) return false;
    }

    uint64_t* pd = get_next_level(pdpt, pdpt_idx, true);
    if (!pd) return false;
    if (pd[pd_idx] & VMM_HUGE) {
        if (!split_huge_2m(pd, pd_idx, flags)) return false;
    }

    uint64_t* pt = get_next_level(pd, pd_idx, true);
    if (!pt) return false;

    pt[pt_idx] = (phys & 0x000FFFFFFFFFF000ULL) | flags | VMM_PRESENT;
    invlpg(virt);
    return true;
}

void vmm_unmap(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx = (virt >> 21) & 0x1FF;
    uint64_t pt_idx = (virt >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level(active_pml4(), pml4_idx, false);
    if (!pdpt) return;

    uint64_t* pd = get_next_level(pdpt, pdpt_idx, false);
    if (!pd) return;

    uint64_t* pt = get_next_level(pd, pd_idx, false);
    if (!pt) return;

    pt[pt_idx] = 0;
    invlpg(virt);
}

uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t* pml4 = (uint64_t*)(read_cr3() + hhdm_offset);
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_PRESENT)) return 0;

    uint64_t* pdpt = (uint64_t*)((pml4[pml4_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    if (!(pdpt[pdpt_idx] & VMM_PRESENT)) return 0;

    // 1GB huge page
    if (pdpt[pdpt_idx] & VMM_HUGE) {
        uint64_t base = pdpt[pdpt_idx] & 0x000FFFFFC0000000ULL; // 1GB aligned
        return base | (virt & 0x3FFFFFFF);
    }

    uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    if (!(pd[pd_idx] & VMM_PRESENT)) return 0;

    // 2MB huge page
    if (pd[pd_idx] & VMM_HUGE) {
        uint64_t base = pd[pd_idx] & 0x000FFFFFFFE00000ULL; // 2MB aligned
        return base | (virt & 0x1FFFFF);
    }

    uint64_t* pt = (uint64_t*)((pd[pd_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    if (!(pt[pt_idx] & VMM_PRESENT)) return 0;

    return (pt[pt_idx] & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFF);
}

int vmm_virt_mapped(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx = (virt >> 21) & 0x1FF;
    uint64_t pt_idx = (virt >> 12) & 0x1FF;

    uint64_t* pml4 = (uint64_t*)(read_cr3() + hhdm_offset);
    if (!(pml4[pml4_idx] & VMM_PRESENT)) return 0;

    uint64_t* pdpt = (uint64_t*)((pml4[pml4_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    if (!(pdpt[pdpt_idx] & VMM_PRESENT)) return 0;

    // Check for 1GB huge page
    if (pdpt[pdpt_idx] & VMM_HUGE) return 1;

    uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    if (!(pd[pd_idx] & VMM_PRESENT)) return 0;

    // Check for 2MB huge page
    if (pd[pd_idx] & VMM_HUGE) return 1;

    uint64_t* pt = (uint64_t*)((pd[pd_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
    if (!(pt[pt_idx] & VMM_PRESENT)) return 0;

    return 1;
}

void vmm_add_flags(uint64_t virt, uint64_t size, uint64_t flags) {
    uint64_t hierarchy_flags = flags & (VMM_USER | VMM_WRITE);
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        uint64_t addr = virt + off;
        uint64_t pml4_idx = (addr >> 39) & 0x1FF;
        uint64_t pdpt_idx = (addr >> 30) & 0x1FF;
        uint64_t pd_idx = (addr >> 21) & 0x1FF;
        uint64_t pt_idx = (addr >> 12) & 0x1FF;

        uint64_t* pml4 = active_pml4();
        if (!(pml4[pml4_idx] & VMM_PRESENT)) continue;
        pml4[pml4_idx] |= hierarchy_flags;

        uint64_t* pdpt = (uint64_t*)((pml4[pml4_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
        if (!(pdpt[pdpt_idx] & VMM_PRESENT)) continue;
        pdpt[pdpt_idx] |= hierarchy_flags;
        if (pdpt[pdpt_idx] & VMM_HUGE) {
            pdpt[pdpt_idx] |= flags;
            invlpg(addr);
            continue;
        }

        uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
        if (!(pd[pd_idx] & VMM_PRESENT)) continue;
        pd[pd_idx] |= hierarchy_flags;
        if (pd[pd_idx] & VMM_HUGE) {
            pd[pd_idx] |= flags;
            invlpg(addr);
            continue;
        }

        uint64_t* pt = (uint64_t*)((pd[pd_idx] & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
        if (!(pt[pt_idx] & VMM_PRESENT)) continue;

        pt[pt_idx] |= flags;
        invlpg(addr);
    }
}

static inline void flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void vmm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    uint64_t* pml4 = active_pml4();
    for (uint64_t i = 0; i < size; i += PAGE_SIZE) {
        uint64_t pml4_idx = ((virt + i) >> 39) & 0x1FF;
        uint64_t pdpt_idx = ((virt + i) >> 30) & 0x1FF;
        uint64_t pd_idx = ((virt + i) >> 21) & 0x1FF;
        uint64_t pt_idx = ((virt + i) >> 12) & 0x1FF;

        uint64_t* pdpt = get_next_level(pml4, pml4_idx, true);
        if (!pdpt) return;
        if (pdpt[pdpt_idx] & VMM_HUGE) {
            if (!split_huge_1g(pdpt, pdpt_idx, flags)) return;
        }
        uint64_t* pd = get_next_level(pdpt, pdpt_idx, true);
        if (!pd) return;
        if (pd[pd_idx] & VMM_HUGE) {
            if (!split_huge_2m(pd, pd_idx, flags)) return;
        }
        uint64_t* pt = get_next_level(pd, pd_idx, true);
        if (!pt) return;

        pt[pt_idx] = ((phys + i) & 0x000FFFFFFFFFF000ULL) | flags | VMM_PRESENT;
    }
    flush_tlb();
}
