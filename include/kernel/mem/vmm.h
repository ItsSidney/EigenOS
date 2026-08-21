/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

// Page table flags
#define VMM_PRESENT    (1ULL << 0)
#define VMM_WRITE      (1ULL << 1)
#define VMM_USER       (1ULL << 2)
#define VMM_PWT        (1ULL << 3)
#define VMM_PCD        (1ULL << 4)
#define VMM_ACCESSED   (1ULL << 5)
#define VMM_DIRTY      (1ULL << 6)
#define VMM_HUGE       (1ULL << 7)
#define VMM_GLOBAL     (1ULL << 8)
#define VMM_NX         (1ULL << 63)

void vmm_init(void);
bool vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
void vmm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
void vmm_add_flags(uint64_t virt, uint64_t size, uint64_t flags);
int vmm_virt_mapped(uint64_t virt);

// Per-process address spaces
uint64_t vmm_get_current_pml4(void);
uint64_t vmm_clone_current_pml4(void);
uint64_t vmm_new_user_pml4(void);   /* fresh minimal AS (kernel half shared) */
void vmm_activate_pml4(uint64_t pml4_phys);

// Physical PML4 used by all ring-0 tasks (set by vmm_init)
extern uint64_t vmm_kernel_pml4_phys;

// Physical Memory Manager
void pmm_init(void);
uint64_t pmm_alloc(void);
void pmm_free(uint64_t phys);
size_t pmm_free_count(void);

#endif
