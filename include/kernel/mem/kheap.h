/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

// Simple kernel heap allocator
// Uses a static pool of memory for kernel allocations

#define KHEAP_SIZE (1024 * 1024 * 128)  // 128MB kernel heap (1080p back buffer alone needs ~8MB)

// Initialize kernel heap
void kheap_init(void);

// Allocate memory (aligned to 4 bytes)
void* kmalloc(size_t size);

// Allocate aligned memory (for DMA)
void* kmalloc_aligned(size_t size, size_t align);

// Free memory
void kfree(void* ptr);
void kfree_aligned(void* ptr);

// Get free memory
size_t kheap_free(void);

// Debug: find the heap block whose payload contains `addr`. Fills
// out_start/out_size (payload range) and out_free (block state).
// Returns 1 if found, 0 if addr is outside the heap.
int kheap_block_info(uint64_t addr, uint64_t* out_start, uint64_t* out_size, int* out_free);

// Debug: dump the block chain. Fills parallel arrays (max `max` entries):
// payload start, size, free flag, next-block pointer. Returns count walked.
int kheap_chain_dump(uint64_t* out_start, uint64_t* out_size, int* out_free, uint64_t* out_next, int max);

// Debug: ring trace of the last kmalloc/kfree calls (ptr, size, is_free,
// caller RIP). Returns the number of entries (<= max).
int kheap_trace_dump(uint64_t* out_ptr, uint64_t* out_size, int* out_free, uint64_t* out_caller, int max);

// Debug: scan the full ring for events matching `target` (callers + type).
int kheap_trace_find(uint64_t target, uint64_t* out_ptr, uint64_t* out_size, int* out_free, uint64_t* out_caller, int max);

#endif
