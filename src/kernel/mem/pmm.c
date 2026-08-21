/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/mem/vmm.h"
#include "limine.h"
#include <stddef.h>

extern struct limine_memmap_request memmap_request;
extern uint64_t hhdm_offset;

static uint64_t* free_pages_stack;
static size_t free_pages_count = 0;

//extern void serial_puts(const char* s);

void pmm_init(void) {
//    serial_puts("[PMM] Initializing...\n");
    if (memmap_request.response == NULL) {
//        serial_puts("[PMM] ERROR: No memmap response\n");
        return;
    }

    uint64_t total_pages = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_pages += entry->length / PAGE_SIZE;
        }
    }

    // Find a spot for our free pages stack
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= total_pages * sizeof(uint64_t)) {
            free_pages_stack = (uint64_t*)(entry->base + hhdm_offset);
//            serial_puts("[PMM] Selected stack at: 0x");
            
            char hex[20];
            const char* hex_chars = "0123456789ABCDEF";
            uint64_t addr = (uint64_t)free_pages_stack;
            for (int j = 0; j < 16; j++) hex[15-j] = hex_chars[(addr >> (j * 4)) & 0x0F];
            hex[16] = 0;
//            serial_puts(hex);
//            serial_puts("\n");
            break;
        }
    }

    if (free_pages_stack == NULL) {
//        serial_puts("[PMM] FATAL: Could not find usable memory for page stack\n");
        return;
    }

    uint64_t stack_phys = (uint64_t)free_pages_stack - hhdm_offset;
    uint64_t stack_size = total_pages * sizeof(uint64_t);
    uint64_t stack_end = stack_phys + stack_size;

    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start = entry->base;
            uint64_t end = entry->base + entry->length;
            
            for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
                if (addr >= stack_phys && addr < stack_end) continue;
                free_pages_stack[free_pages_count++] = addr;
            }
        }
    }
}

uint64_t pmm_alloc(void) {
    if (free_pages_count == 0) return 0;
    return free_pages_stack[--free_pages_count];
}

void pmm_free(uint64_t phys) {
    free_pages_stack[free_pages_count++] = phys;
}

size_t pmm_free_count(void) {
    return free_pages_count;
}
