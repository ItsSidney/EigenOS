/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/mem/kheap.h"

// Better memory management: First-fit linked-list allocator
#define META_SIZE sizeof(struct block_meta)

typedef struct block_meta {
    size_t size;
    int is_free;
    struct block_meta *next;
} block_meta_t;

static uint8_t heap[KHEAP_SIZE];
static block_meta_t* free_list = NULL;
static int initialized = 0;

/* Serialize kmalloc/kfree: concurrent allocations from multiple tasks
   (GUI, fetches, img tasks, worker) would corrupt the free list and
   hang find_free_block's linked-list walk. Short critical sections;
   preemption + the timer keep a preempted owner running. */
static volatile int kheap_lock = 0;
static volatile int kheap_lock_owner = -1;
static volatile int kheap_lock_depth = 0;

#include "kernel/task/task.h"
extern int get_current_task_id(void);

static void kheap_lock_acquire(void) {
    int me = get_current_task_id();
    if (kheap_lock_owner == me) {
        kheap_lock_depth++;
        return;
    }
    int spins = 0;
    while (__atomic_test_and_set(&kheap_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause" : : : "memory");
        if (++spins > 1000000) {
            /* Should not happen with the preemptive scheduler; back off
               rather than spinning forever on a corrupted free list. */
            spins = 0;
        }
    }
    kheap_lock_owner = me;
    kheap_lock_depth = 1;
}

static void kheap_lock_release(void) {
    if (kheap_lock_depth > 1) {
        kheap_lock_depth--;
        return;
    }
    kheap_lock_owner = -1;
    kheap_lock_depth = 0;
    __atomic_clear(&kheap_lock, __ATOMIC_RELEASE);
}

void kheap_init(void) {
    free_list = (block_meta_t*)heap;
    free_list->size = KHEAP_SIZE - META_SIZE;
    free_list->is_free = 1;
    free_list->next = NULL;
    initialized = 1;
}

static block_meta_t* find_free_block(size_t size) {
    block_meta_t* curr = free_list;
    while (curr) {
        if (curr->is_free && curr->size >= size) return curr;
        curr = curr->next;
    }
    return NULL;
}

static void split_block(block_meta_t* block, size_t size) {
    if (block->size > size + META_SIZE + 32) {
        block_meta_t* new_block = (block_meta_t*)((uint8_t*)block + META_SIZE + size);
        new_block->size = block->size - size - META_SIZE;
        new_block->is_free = 1;
        new_block->next = block->next;
        block->size = size;
        block->next = new_block;
    }
}

static void heap_trace_add(uint64_t ptr, uint64_t size, int is_free, uint64_t caller);

void* kmalloc(size_t size) {
    if (!initialized) kheap_init();
    
    kheap_lock_acquire();

    // Align to 8 bytes
    size = (size + 7) & ~7;
    
    block_meta_t* block = find_free_block(size);
    if (!block) {
        kheap_lock_release();
        return NULL;
    }
    
    split_block(block, size);
    block->is_free = 0;
    
    void* result = (void*)((uint8_t*)block + META_SIZE);

    /* GUARD: if a fresh allocation lands inside a live ring-3 window buffer,
       the free list was corrupted (the window block's header was marked free
       by an overflow from the preceding block). Log the culprit + caller and
       dump the corrupted header bytes so we can identify the overflowing write. */
    {
        extern int wm_dump_user_windows(uint64_t* ids, uint64_t* bs, uint64_t* cs, int* ps, int max);
        static uint64_t g_wid[8], g_wbuf[8], g_wcbuf[8]; static int g_wpid[8];
        int gn = wm_dump_user_windows(g_wid, g_wbuf, g_wcbuf, g_wpid, 8);
        for (int gi = 0; gi < gn; gi++) {
            if (g_wbuf[gi] && (uint64_t)result >= g_wbuf[gi] && (uint64_t)result < g_wbuf[gi] + 0x400000) {
                uint64_t caller = (uint64_t)__builtin_return_address(0);
                extern void serial_puts(const char*);
                const char* hx = "0123456789ABCDEF";
                serial_puts("[HEAP] DOUBLE-ALLOC inside window buf id=");
                { uint64_t v = g_wid[gi]; char o[21]; int n=0; char tmp[21]; int tn=0;
                  if (!v) tmp[tn++]='0'; else while(v){tmp[tn++]=hx[v&0xF];v>>=4;}
                  while(tn>0) o[n++]=tmp[--tn]; o[n]=0; serial_puts(o); }
                serial_puts(" caller=0x");
                { uint64_t v=caller; char o[17]; for(int k=0;k<16;k++){o[15-k]=hx[(v>>(k*4))&0xF];} o[16]=0; serial_puts(o); }
                serial_puts(" hdr(buf-24,8B):");
                uint8_t* hb = (uint8_t*)((uint8_t*)result - 24);
                for (int b = 0; b < 8; b++) {
                    serial_puts(" ");
                    { uint8_t v=hb[b]; char o[3]; o[0]=hx[v>>4]; o[1]=hx[v&0xF]; o[2]=0; serial_puts(o); }
                }
                serial_puts("\n");
            }
        }
    }

    heap_trace_add((uint64_t)result, size, 0, (uint64_t)__builtin_return_address(0));
    kheap_lock_release();
    return result;
}

void* kmalloc_aligned(size_t size, size_t align) {
    if (!initialized) kheap_init();
    
    // Allocate extra space for alignment and to store the original pointer
    size_t total_size = size + align + sizeof(void*);
    void* ptr = kmalloc(total_size);
    if (!ptr) return NULL;
    
    uintptr_t addr = (uintptr_t)ptr + sizeof(void*);
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);
    
    // Store the original pointer right before the aligned address
    ((void**)aligned)[-1] = ptr;
    
    return (void*)aligned;
}

void kfree_aligned(void* ptr) {
    if (!ptr) return;
    // Retrieve original pointer stored before the aligned address
    void* original = ((void**)ptr)[-1];
    kfree(original);
}

static void merge_free_blocks() {
    block_meta_t* curr = free_list;
    int safety = 0;
    while (curr && curr->next && (uint8_t*)curr >= heap && (uint8_t*)curr < heap + KHEAP_SIZE && ++safety < 100000) {
        if ((uint8_t*)curr->next < heap || (uint8_t*)curr->next >= heap + KHEAP_SIZE) {
            curr->next = NULL;
            break;
        }
        if (curr->is_free && curr->next->is_free) {
            curr->size += META_SIZE + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

/* ── Allocation trace (crash diagnostics) ─────────────────────
   Ring of the last kmalloc/kfree events with caller RIPs, dumped by
   core_exception_handler to identify who freed/reused a clobbered block. */
#define HEAP_TRACE_MAX 4096
typedef struct {
    uint64_t ptr;
    uint64_t size;
    int is_free;
    uint64_t caller;
} heap_trace_t;
static heap_trace_t heap_trace[HEAP_TRACE_MAX];
static volatile int heap_trace_count = 0;

static void heap_trace_add(uint64_t ptr, uint64_t size, int is_free, uint64_t caller) {
    int idx = heap_trace_count % HEAP_TRACE_MAX;
    heap_trace[idx].ptr = ptr;
    heap_trace[idx].size = size;
    heap_trace[idx].is_free = is_free;
    heap_trace[idx].caller = caller;
    heap_trace_count++;
}

int kheap_trace_dump(uint64_t* out_ptr, uint64_t* out_size, int* out_free, uint64_t* out_caller, int max) {
    int total = heap_trace_count;
    int n = 0;
    int idx = total % HEAP_TRACE_MAX;
    while (n < max && n < total && n < HEAP_TRACE_MAX) {
        int i = (idx - 1 - n + HEAP_TRACE_MAX * 2) % HEAP_TRACE_MAX;
        if (i < 0) i += HEAP_TRACE_MAX;
        if (out_ptr) out_ptr[n] = heap_trace[i].ptr;
        if (out_size) out_size[n] = heap_trace[i].size;
        if (out_free) out_free[n] = heap_trace[i].is_free;
        if (out_caller) out_caller[n] = heap_trace[i].caller;
        n++;
    }
    return n;
}

/* Scan the ENTIRE ring for events whose ptr matches `target` (e.g. a window
   buffer address) and copy them out. Used by the crash dump to name the exact
   kmalloc/kfree that touched a clobbered block. */
int kheap_trace_find(uint64_t target, uint64_t* out_ptr, uint64_t* out_size,
                     int* out_free, uint64_t* out_caller, int max) {
    int total = heap_trace_count;
    int n = 0;
    for (int i = 0; i < total && i < HEAP_TRACE_MAX && n < max; i++) {
        if (heap_trace[i].ptr == target) {
            if (out_ptr) out_ptr[n] = heap_trace[i].ptr;
            if (out_size) out_size[n] = heap_trace[i].size;
            if (out_free) out_free[n] = heap_trace[i].is_free;
            if (out_caller) out_caller[n] = heap_trace[i].caller;
            n++;
        }
    }
    return n;
}

void kfree(void* ptr) {
    if (!ptr) return;
    kheap_lock_acquire();
    block_meta_t* block = (block_meta_t*)((uint8_t*)ptr - META_SIZE);
    
    // Basic bounds check
    if ((uint8_t*)block >= heap && (uint8_t*)block < heap + KHEAP_SIZE) {
        block->is_free = 1;
        heap_trace_add((uint64_t)ptr, 0, 1, (uint64_t)__builtin_return_address(0));
        merge_free_blocks();
    }
    kheap_lock_release();
}

size_t kheap_free(void) {
    if (!initialized) return KHEAP_SIZE;
    kheap_lock_acquire();
    size_t total = 0;
    block_meta_t* curr = free_list;
    int safety = 0;
    while (curr && (uint8_t*)curr >= heap && (uint8_t*)curr < heap + KHEAP_SIZE && ++safety < 100000) {
        if (curr->is_free) total += curr->size;
        if ((uint8_t*)curr->next < heap || (uint8_t*)curr->next >= heap + KHEAP_SIZE) break;
        curr = curr->next;
    }
    kheap_lock_release();
    return total;
}

int kheap_block_info(uint64_t addr, uint64_t* out_start, uint64_t* out_size, int* out_free) {
    if (!initialized) return 0;
    block_meta_t* curr = free_list;
    int safety = 0;
    while (curr && (uint8_t*)curr >= heap && (uint8_t*)curr < heap + KHEAP_SIZE && ++safety < 100000) {
        uint64_t s = (uint64_t)((uint8_t*)curr + META_SIZE);
        uint64_t e = s + curr->size;
        if (addr >= s && addr < e) {
            if (out_start) *out_start = s;
            if (out_size) *out_size = curr->size;
            if (out_free) *out_free = curr->is_free;
            return 1;
        }
        if ((uint8_t*)curr->next < heap || (uint8_t*)curr->next >= heap + KHEAP_SIZE) break;
        curr = curr->next;
    }
    return 0;
}

int kheap_chain_dump(uint64_t* out_start, uint64_t* out_size, int* out_free, uint64_t* out_next, int max) {
    if (!initialized) return 0;
    block_meta_t* curr = free_list;
    int n = 0;
    int safety = 0;
    while (curr && n < max && ++safety < 100000) {
        out_start[n] = (uint64_t)((uint8_t*)curr + META_SIZE);
        out_size[n] = curr->size;
        out_free[n] = curr->is_free;
        out_next[n] = (uint64_t)(uintptr_t)curr->next;
        n++;
        if ((uint8_t*)curr->next < heap || (uint8_t*)curr->next >= heap + KHEAP_SIZE) break;
        curr = curr->next;
    }
    return n;
}
