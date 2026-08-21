/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/syscall.h"
#include "kernel/task/task.h"
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include "kernel/time/timer.h"
#include "kernel/elf.h"
#include "kernel/log.h"
#include "user/eigen.h"
#include "filesystem/filesystem.h"
#include "gui/wm.h"
#include "gui/gui.h"
#include "libs/bmp.h"
#include "drivers/time/rtc.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/gfx.h"
#include "drivers/input/keyboard.h"
#include "drivers/audio/speaker.h"
#include "filesystem/filesystem.h"
#include "filesystem/vfs.h"
#include "kernel/net/socket.h"
#include "kernel/net/in.h"
#include <string.h>

typedef uint64_t (*syscall_fn)(uint64_t, uint64_t, uint64_t, uint64_t);

/* User module registry (populated by the kernel from Limine modules). */
extern int user_module_find(const char* name, const void** data, uint64_t* size);
extern uint64_t get_total_memory_bytes(void);
extern int get_task_count(void);
extern int create_user_process_elf(const char* name);
/* Live OS theme palette (theme.c). Declared here so we can export the
   active palette to ring-3 apps without pulling in the whole gui.h. */
#include "gui/gui.h"

/* ------------------------------------------------------------------ */
/* User-space heap: free-list allocator over PMM pages mapped USER.    */
/*                                                                     */
/* The old allocator was a pure bump allocator with a no-op free(),     */
/* which worked only for apps that never release memory. Nuklear (and    */
/* any app that realloc/free) leaked every freed block into the 64 MB   */
/* region until the cursor hit the ceiling and alloc() returned NULL —   */
/* which Nuklear dereferences unchecked, faulting the ring-3 task and   */
/* closing its window. This version keeps a block list (every live       */
/* allocation, free or allocated) so free() can find the exact block by  */
/* base and coalesce neighbours; the heap reuses freed spans instead of  */
/* leaking them.                                                       */
/* ------------------------------------------------------------------ */
#define USER_HEAP_BASE  0x20000000ULL
#define USER_HEAP_LIMIT (USER_HEAP_BASE + 64ULL * 1024 * 1024)

typedef struct user_blk {
    uint64_t base;          /* user virtual address of the block        */
    uint64_t npages;        /* size in 4 KiB pages                     */
    int      free;          /* 1 = on the free list, 0 = allocated     */
    struct user_blk* next;
    struct user_blk* prev;
} user_blk_t;

static user_blk_t* user_blocks = 0;          /* all blocks, ordered by base */
static uint64_t     user_heap_cur = USER_HEAP_BASE;  /* bump high-water mark */

/* Map `npages` fresh PMM pages at user virtual `base` (zeroed). */
static int user_map_pages(uint64_t base, uint64_t npages) {
    for (uint64_t p = 0; p < npages; p++) {
        uint64_t phys = pmm_alloc();
        if (!phys) return -1;
        vmm_map_range(base + p * 4096, phys, 4096,
                      VMM_PRESENT | VMM_WRITE | VMM_USER);
        __builtin_memset((void*)(uintptr_t)(base + p * 4096), 0, 4096);
    }
    return 0;
}

/* Allocate a descriptor and link it into `user_blocks` in ascending-base
   order (prev/next bookkeeping only; the block itself is not yet free). */
static user_blk_t* user_blk_new(uint64_t base, uint64_t npages, int free) {
    user_blk_t* b = (user_blk_t*)kmalloc(sizeof(user_blk_t));
    if (!b) return 0;
    b->base = base; b->npages = npages; b->free = free;
    user_blk_t* cur = user_blocks, *prev = 0;
    while (cur && cur->base < base) { prev = cur; cur = cur->next; }
    b->prev = prev; b->next = cur;
    if (prev) prev->next = b; else user_blocks = b;
    if (cur) cur->prev = b;
    return b;
}

/* Coalesce a just-freed block with its (now) free neighbours. */
static void user_blk_coalesce(user_blk_t* b) {
    /* merge with previous if contiguous + free */
    if (b->prev && b->prev->free &&
        b->prev->base + b->prev->npages * 4096 == b->base) {
        user_blk_t* p = b->prev;
        p->npages += b->npages;
        p->next = b->next;
        if (b->next) b->next->prev = p;
        kfree(b);
        b = p;
    }
    /* merge with next if contiguous + free */
    if (b->next && b->next->free &&
        b->base + b->npages * 4096 == b->next->base) {
        user_blk_t* n = b->next;
        b->npages += n->npages;
        b->next = n->next;
        if (n->next) n->next->prev = b;
        kfree(n);
    }
}

static uint64_t k_sys_alloc_user(uint64_t size) {
    if (size == 0) return 0;
    /* Round up to whole pages (user ABI hands back page-aligned blocks). */
    uint64_t npages = (size + 4095) / 4096;
    if (npages == 0) npages = 1;

    /* 1) Find the first free block large enough (first-fit). */
    for (user_blk_t* b = user_blocks; b; b = b->next) {
        if (b->free && b->npages >= npages) {
            /* Split off the tail if the block is bigger than needed. */
            if (b->npages > npages) {
                user_blk_new(b->base + npages * 4096, b->npages - npages, 1);
                b->npages = npages;
            }
            b->free = 0;
            return b->base;
        }
    }

    /* 2) Otherwise grow the mapped region (bump the high-water mark). */
    uint64_t base = user_heap_cur;
    if (base + npages * 4096 > USER_HEAP_LIMIT) return -1;
    if (user_map_pages(base, npages) != 0) return -1;
    user_heap_cur += npages * 4096;
    user_blk_new(base, npages, 0);
    return base;
}

/* Free a user block: locate it by base in the block list (so its real
   size is known exactly), mark it free, and coalesce neighbours. */
static void k_sys_free_user(uint64_t ptr) {
    if (!ptr || ptr < USER_HEAP_BASE || ptr >= USER_HEAP_LIMIT) return;
    for (user_blk_t* b = user_blocks; b; b = b->next) {
        if (b->base == ptr) {
            if (b->free) return;            /* double free — ignore */
            b->free = 1;
            user_blk_coalesce(b);
            return;
        }
    }
    /* Unknown base: ignore (defensive; shouldn't happen with our ABI). */
}

/* ------------------------------------------------------------------ */
/* Existing syscalls (stable numbers 0..9)                             */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_exit(uint64_t code, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    task_t* cur = get_current_task();
    if (cur && cur->group > 0) {
        /* A process exit strands every thread of the group: their shared
           address space is going away with the leader. */
        kill_thread_group(cur->group);
    }
    exit_task((int)code);
    return 0;
}

static uint64_t k_sys_write(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a4) {
    (void)a4;
    if (fd == 1 || fd == 2) {
        // Route stdout/stderr to the active GUI terminal when present
        // (visible where you typed `spawn`), else the kernel log.
        if (count > 255) count = 255;
        char tmp[256];
        memcpy(tmp, (const char*)buf, (size_t)count);
        tmp[count] = 0;
        extern int gui_running;
        klog(tmp);
        extern void serial_puts(const char* s);
        serial_puts(tmp);
        return count;
    }
    return (uint64_t)sys_write((int)fd, (char*)buf, (uint32_t)count);
}

static uint64_t k_sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a4) {
    (void)a4;
    return (uint64_t)sys_read((int)fd, (char*)buf, (uint32_t)count);
}

static uint64_t k_sys_open(uint64_t path, uint64_t flags, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    return (uint64_t)sys_open((const char*)path, (int)flags);
}

static uint64_t k_sys_close(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    sys_close((int)fd);
    return 0;
}

static uint64_t k_sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    task_t* cur = get_current_task();
    return cur ? (uint64_t)cur->id : 0;
}

static uint64_t k_sys_sleep(uint64_t ms, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    sleep_task((uint32_t)ms);
    return 0;
}

static uint64_t k_sys_gettime(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    return timer_get_ms();
}

static uint64_t k_sys_alloc(uint64_t size, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    return k_sys_alloc_user(size);
}

static uint64_t k_sys_free(uint64_t ptr, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    k_sys_free_user(ptr);
    return 0;
}

/* ------------------------------------------------------------------ */
/* New API81 syscalls (numbers 10..15)                                 */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_spawn(uint64_t name, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2;
    if (!name || name >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    int argc = (int)a4;
    if (argc > 0) {
        /* argv is a user-space NULL-terminated array of user pointers. */
        if (!a3 || a3 >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
        if (argc > MAX_ARGS) argc = MAX_ARGS;
        uint64_t* uargv = (uint64_t*)(uintptr_t)a3;
        char* kargv[MAX_ARGS] = {0};
        int n = 0;
        for (; n < argc; n++) {
            uint64_t p = uargv[n];
            if (!p || p >= 0x800000000000ULL) break;
            size_t len = 0;
            while (((char*)(uintptr_t)p)[len] && len < 1023) len++;
            char* k = (char*)kmalloc(len + 1);
            if (!k) break;
            for (size_t j = 0; j < len; j++) k[j] = ((char*)(uintptr_t)p)[j];
            k[len] = 0;
            kargv[n] = k;
        }
        uint64_t rc = (uint64_t)create_user_process_elf_args((const char*)name, n, kargv);
        for (int i = 0; i < n; i++) if (kargv[i]) kfree(kargv[i]);
        return rc;
    }
    return (uint64_t)create_user_process_elf((const char*)name);
}

static uint64_t k_sys_wait(uint64_t pid, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    for (;;) {
        task_t* t = get_task_by_id((int)pid);
        if (!t) return EIGEN_ERR_NOPROC;
        if (t->state == TASK_DEAD) return (uint64_t)(uint32_t)t->exit_code;
        sleep_task(10);
    }
}

static uint64_t k_sys_sysinfo(uint64_t buf, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    if (!buf || buf >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    struct eigen_sysinfo* si = (struct eigen_sysinfo*)buf;
    si->api_version = EIGEN_API_VERSION;
    si->total_mem_kb = (uint32_t)(get_total_memory_bytes() / 1024);
    si->timer_hz = timer_hz;
    si->screen_w = get_fb_width();
    si->screen_h = get_fb_height();
    si->task_count = (uint32_t)get_task_count();
    si->uptime_ms = timer_get_ms();
    return 0;
}

static uint64_t k_sys_gfx(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
        case EIGEN_GFX_GETDIMS: {
            if (!a || a >= 0x800000000000ULL) return EIGEN_ERR_BADSCR;
            if (!b || b >= 0x800000000000ULL) return EIGEN_ERR_BADSCR;
            *(uint32_t*)a = get_fb_width();
            *(uint32_t*)b = get_fb_height();
            return 0;
        }
        case EIGEN_GFX_PUTPIXEL:
            put_pixel((uint32_t)a, (uint32_t)b, (uint32_t)c);
            return 0;
        case EIGEN_GFX_FILLRECT: {
            int x = (int)(int16_t)((a >> 16) & 0xFFFF), y = (int)(int16_t)(a & 0xFFFF);
            int w = (int)(int16_t)((b >> 16) & 0xFFFF), h = (int)(int16_t)(b & 0xFFFF);
            gfx_fill_rect(x, y, w, h, (uint32_t)c);
            return 0;
        }
        case EIGEN_GFX_SWAP:
            swap_buffers();
            return 0;
        default:
            return EIGEN_ERR_INVAL;
    }
}

static uint64_t k_sys_input(uint64_t op, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    switch (op) {
        case EIGEN_INPUT_KBHIT:   return (uint64_t)keyboard_has_input();
        case EIGEN_INPUT_GETCHAR: return (uint64_t)(unsigned char)get_key();
        case EIGEN_INPUT_DRAIN:   keyboard_drain(); return 0;
        default: return EIGEN_ERR_INVAL;
    }
}

static uint64_t k_sys_beep(uint64_t freq, uint64_t ms, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (freq < 20 || freq > 20000) return EIGEN_ERR_INVAL;
    speaker_play_freq((uint32_t)freq);
    sleep_task((uint32_t)ms);
    speaker_stop();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Ring-3 windows (EIGEN_SYS_WIN)                                      */
/* ------------------------------------------------------------------ */
#define FONT_MAP_VMA 0x7C000000ULL
extern const uint8_t font8x16[];

static uint64_t k_sys_win(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
    case EIGEN_WIN_CREATE: {
        char title[64];
        title[0] = 0;
        if (c && c < 0x800000000000ULL) {
            memcpy(title, (const char*)c, 63);
            title[63] = 0;
        }
        int x = (int)((a >> 16) & 0xFFFF), y = (int)(a & 0xFFFF);
        int w = (int)((b >> 16) & 0xFFFF), h = (int)(b & 0xFFFF);
        if (w < 16) w = 16;
        if (h < 24) h = 24;
        if (w > 2048) w = 2048;
        if (h > 2048) h = 2048;
        extern void serial_puts(const char* s);
        extern void serial_u64(uint64_t v);
        uint32_t wt0 = timer_get_ms();
        uint64_t wid = (uint64_t)wm_open_window_user(x, y, w, h, title, 0x007AFF);
        serial_puts("[S] win create "); serial_u64(w); serial_puts("x"); serial_u64(h);
        serial_puts(" took "); serial_u64((uint64_t)(timer_get_ms() - wt0)); serial_puts(" ms\n");
        return wid;
    }
    case EIGEN_WIN_MAP:
        return (uint64_t)wm_user_map_buffer((int)a);
    case EIGEN_WIN_FLUSH:
        return (uint64_t)wm_user_flush((int)a);
    case EIGEN_WIN_CLOSE:
        wm_close_window((int)a);
        return 0;
    case EIGEN_WIN_POLL: {
        if (!b || b >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
        if (c > 64) c = 64;
        if (c == 0) return 0;
        wm_user_ev_t evs[64];
        int n = wm_user_poll((int)a, evs, (int)c);
        if (n < 0) return n;
        memcpy((void*)b, evs, (size_t)n * sizeof(wm_user_ev_t));
        return (uint64_t)n;
    }
    case EIGEN_WIN_MAXIMIZE: {
        wm_window_t* win = wm_get_window((int)a);
        if (!win) return EIGEN_ERR_INVAL;
        if (win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V))
            wm_restore_window((int)a);
        else
            wm_maximize_window((int)a, WM_MAX_FULL);
        return 0;
    }
    case EIGEN_WIN_SETTITLE:
        return 0;
    case EIGEN_WIN_GETSIZE: {
        wm_window_t* win = wm_get_window((int)a);
        if (!win || !b || b >= 0x800000000000ULL
            || !c || c >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
        /* Report the app buffer size (user_buf_w/h), which only changes in
           wm_user_flush() between the app's frames — NOT the WM window size
           (content_w/h), which updates mid-frame during a resize/maximize.
           Reporting the WM size would let the app lay out at a size its
           buffer hasn't been switched to yet: the flush then copies the
           wrong byte count (garbage strips / cut-off frames). */
        *(uint32_t*)b = (uint32_t)win->user_buf_w;
        *(uint32_t*)c = (uint32_t)win->user_buf_h;
        return 0;
    }
    }
    return EIGEN_ERR_INVAL;
}

/* ------------------------------------------------------------------ */
/* Time-of-day (EIGEN_SYS_TIME)                                        */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_time(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    (void)b; (void)c;
    if (op != EIGEN_TIME_GET) return EIGEN_ERR_INVAL;
    if (!a || a >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    time_t t;
    get_time(&t);
    int vals[6] = { t.hour, t.minute, t.second, t.day, t.month, t.year };
    memcpy((void*)a, vals, sizeof(vals));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Kernel font mapping (EIGEN_SYS_FONTMAP)                             */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_fontmap(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    (void)a; (void)b; (void)c;
    if (op != EIGEN_FONTMAP_MAP) return EIGEN_ERR_INVAL;
    uint64_t phys = vmm_get_phys((uint64_t)(uintptr_t)font8x16);
    if (!phys) return EIGEN_ERR_NOMEM;
    uint64_t off = phys & 0xFFF;
    /* font is exactly 4096 bytes but may straddle a page: map 2 pages */
    vmm_map_range(FONT_MAP_VMA, phys & ~0xFFFULL, 8192,
                  VMM_PRESENT | VMM_WRITE | VMM_USER);
    return FONT_MAP_VMA + off;
}

/* ------------------------------------------------------------------ */
/* Live theme export (EIGEN_SYS_GET_THEME)                             */
/* Copies the active OS theme palette into a ring-3 array so user apps */
/* look like the shell instead of a hard-coded off-brand palette.      */
/* args: (op=ignored, user_array_ptr, max_count, 0) -> entries copied  */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_gettheme(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    (void)c; (void)d;
    if (!a || a >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    int n = (int)b;
    if (n < 1) n = EIGEN_THEME_COUNT;
    if (n > EIGEN_THEME_COUNT) n = EIGEN_THEME_COUNT;
    uint32_t* out = (uint32_t*)(uintptr_t)a;
    for (int i = 0; i < n; i++) {
        /* theme_get_color() bounds-checks the role and returns a safe
           default for any out-of-range slot, so we can always copy n. */
        out[i] = theme_get_color(i);
    }
    return (uint64_t)n;
}

/* ------------------------------------------------------------------ */
/* Boot-module loader (EIGEN_SYS_MODLOAD)                             */
/* Copies the bytes of a named Limine boot module into a user buffer. */
/* Used by ring-3 apps (e.g. DOOM) to fetch their data files (WAD).    */
/* args: (op, name_ptr, dest_ptr, max_bytes) -> bytes copied or <0     */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_modload(uint64_t name, uint64_t dest, uint64_t maxbytes, uint64_t a4) {
    (void)a4;
    if (!name || name >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    if (!dest || dest >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    if (maxbytes == 0 || maxbytes > 64ULL * 1024 * 1024) return EIGEN_ERR_INVAL;

    const void* mdata = 0;
    uint64_t msize = 0;
    if (user_module_find((const char*)name, &mdata, &msize) != 0 || !mdata || msize == 0)
        return EIGEN_ERR_NOENT;

    uint64_t copy = msize < maxbytes ? msize : maxbytes;
    extern void serial_puts(const char* s);
    extern void serial_u64(uint64_t v);
    uint32_t mt0 = timer_get_ms();
    memcpy((void*)(uintptr_t)dest, mdata, (size_t)copy);
    serial_puts("[S] modload "); serial_u64(copy); serial_puts(" bytes took "); serial_u64((uint64_t)(timer_get_ms() - mt0)); serial_puts(" ms\n");
    return copy;
}

/* ------------------------------------------------------------------ */
/* Filesystem (EIGEN_SYS_FS)                                           */
/* Ring-3 apps can create folders/files, delete, rename, list and      */
/* query sizes. Paths are relative to the fs working directory.        */
/* ------------------------------------------------------------------ */
static int user_ptr_ok(uint64_t p) {
    return p >= 0x1000 && p < 0x800000000000ULL;
}

static uint64_t k_sys_fs(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
    case EIGEN_FS_MKDIR:
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        return (uint64_t)(int)fs_mkdir((const char*)a);
    case EIGEN_FS_CREATE: {
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        int fd = fs_create((const char*)a);
        if (fd < 0) return EIGEN_ERR_NOENT;
        fs_close(fd);                       /* keep fds clean for the app */
        return 0;
    }
    case EIGEN_FS_DELETE:
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        return (uint64_t)(int)fs_delete((const char*)a);
    case EIGEN_FS_RMDIR:
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        return (uint64_t)(int)fs_rmdir((const char*)a);
    case EIGEN_FS_EXISTS:
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        return fs_exists((const char*)a) ? 1 : 0;
    case EIGEN_FS_RENAME:
        if (!user_ptr_ok(a) || !user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        return (uint64_t)(int)fs_rename((const char*)a, (const char*)b);
    case EIGEN_FS_LIST: {
        if (!user_ptr_ok(a) || b == 0 || b > 8192) return EIGEN_ERR_INVAL;
        char tmp[8192];
        int n = fs_list(tmp, sizeof(tmp) - 1);
        if (n < 0) return n;
        if (n > (int)b) n = (int)b;
        memcpy((void*)(uintptr_t)a, tmp, (size_t)n);
        return (uint64_t)n;
    }
    case EIGEN_FS_SIZE: {
        if (!user_ptr_ok(a) || !user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        int idx = fs_open((const char*)a, 0);
        if (idx < 0) return EIGEN_ERR_NOENT;
        int size = 0;
        fs_get_node(idx, 0, &size, 0, 0, 0, 0);
        *(uint32_t*)(uintptr_t)b = (uint32_t)size;
        return 0;
    }
    case EIGEN_FS_TRUNCATE:
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        return (uint64_t)(int)fs_truncate((const char*)a);
    case EIGEN_FS_STAT: {
        if (!user_ptr_ok(a) || !user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        int idx = fs_open((const char*)a, 0);
        if (idx < 0) return EIGEN_ERR_NOENT;
        struct eigen_stat* st = (struct eigen_stat*)(uintptr_t)b;
        int size = 0, type = 0;
        uint32_t mt = 0;
        fs_get_node(idx, st->name, &size, &type, 0, 0, &mt);
        st->size = (uint32_t)size;
        st->type = (uint32_t)type;
        st->mtime = mt;
        return 0;
    }
    case EIGEN_FS_CHDIR:
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        return (uint64_t)(int)fs_cd((const char*)a);
    case EIGEN_FS_PWD: {
        if (!user_ptr_ok(a) || b == 0 || b > 1024) return EIGEN_ERR_INVAL;
        char tmp[1024];
        int n = fs_pwd(tmp, sizeof(tmp) - 1);
        if (n < 0) return n;
        if (n > (int)b) n = (int)b;
        memcpy((void*)(uintptr_t)a, tmp, (size_t)n);
        ((char*)(uintptr_t)a)[n < (int)b ? n : (int)b - 1] = 0;
        return (uint64_t)n;
    }
    default:
        return EIGEN_ERR_INVAL;
    }
}

static uint64_t k_sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a4) {
    (void)a4;
    return (uint64_t)sys_lseek((int)fd, (int)offset, (int)whence);
}

/* ------------------------------------------------------------------ */
/* System settings (EIGEN_SYS_SETTINGS)                                */
/* Ring-3 Settings app: themes (incl. user-defined), accent, wallpaper */
/* and desktop-icon mask. All ops touch the live kernel state.         */
/* ------------------------------------------------------------------ */
#include "gui/wallpaper_mgr.h"
static uint64_t k_sys_settings(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
    case EIGEN_SETTINGS_THEME_APPLY:
        theme_apply((theme_id_t)(int)a);
        return 0;
    case EIGEN_SETTINGS_THEME_TOTAL:
        return (uint64_t)theme_total();
    case EIGEN_SETTINGS_THEME_GET: {
        if (!user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        int id = (int)a;
        if (id < 0 || id >= theme_total()) return EIGEN_ERR_INVAL;
        theme_def_t* t = theme_def((theme_id_t)id);
        eigen_theme_info_t* out = (eigen_theme_info_t*)(uintptr_t)b;
        memset(out->name, 0, sizeof(out->name));
        int nn = 0; while (t->name[nn] && nn < (int)sizeof(out->name) - 1) nn++;
        memcpy(out->name, t->name, (size_t)nn);
        memset(out->palette, 0, sizeof(out->palette));
        for (int r = 0; r < THEME_ROLE_COUNT; r++)
            out->palette[r] = t->palette[r];
        out->accent_idx = t->accent_idx;
        out->is_user = theme_is_user((theme_id_t)id) ? 1 : 0;
        return 0;
    }
    case EIGEN_SETTINGS_THEME_ADD: {
        if (!user_ptr_ok(a) || !user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        const char* name = (const char*)(uintptr_t)a;
        const uint32_t* pal = (const uint32_t*)(uintptr_t)b;
        uint32_t tmp[THEME_ROLE_COUNT];
        memcpy(tmp, pal, sizeof(tmp));
        theme_id_t id = theme_user_add(name, tmp, 0, (int)c);
        return (id < 0) ? (uint64_t)EIGEN_ERR_NOMEM : (uint64_t)id;
    }
    case EIGEN_SETTINGS_THEME_DEL:
        theme_user_del((theme_id_t)(int)a);
        return 0;
    case EIGEN_SETTINGS_ACCENT_SET:
        theme_set_accent((int)a);
        return 0;
    case EIGEN_SETTINGS_ACCENT_GET:
        return (uint64_t)get_personalization()->accent_color_idx;
    case EIGEN_SETTINGS_ACCENT_COLOR: {
        extern uint32_t accent_color_at(int idx);
        uint32_t col = accent_color_at((int)a);
        return col ? (uint64_t)col : (uint64_t)EIGEN_ERR_INVAL;
    }
    case EIGEN_SETTINGS_WALL_COUNT:
        return (uint64_t)wallpaper_mgr_count();
    case EIGEN_SETTINGS_WALL_GET: {
        if (!user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        return (uint64_t)wallpaper_mgr_get((int)a, (wp_entry_t*)(uintptr_t)b);
    }
    case EIGEN_SETTINGS_WALL_APPLY:
        wallpaper_mgr_apply((int)a);
        return 0;
    case EIGEN_SETTINGS_WALL_DEFAULT:
        wallpaper_mgr_apply_default();
        return 0;
    case EIGEN_SETTINGS_WALL_PROC:
        wallpaper_mgr_apply_procedural((int)a);
        return 0;
    case EIGEN_SETTINGS_WALL_THUMB: {
        /* args: (ptr, 0, 0) where ptr -> {int idx; uint32_t* fb; int fbw;
         *                             int box[4];}  (box = x, y, w, h)     */
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        int arg[8];
        memcpy(arg, (void*)(uintptr_t)a, sizeof(arg));
        int idx = arg[0], fbw = arg[2], box_x = arg[3], box_y = arg[4];
        int box_w = arg[5], box_h = arg[6];
        (void)box_y;
        uint32_t* fb = (uint32_t*)(uintptr_t)arg[1];
        if (!user_ptr_ok((uint64_t)arg[1])) return EIGEN_ERR_INVAL;
        if (box_w < 1 || box_h < 1) return EIGEN_ERR_INVAL;
        bmp_image_t img;
        if (!wallpaper_mgr_get_thumb(idx, &img)) return EIGEN_ERR_NOENT;
        if (!img.pixels) return EIGEN_ERR_NOENT;
        /* scale to the box (contain, centered) */
        int tw = img.width, th = img.height;
        float s = (float)box_w / (float)tw;
        float sh = (float)box_h / (float)th;
        if (sh < s) s = sh;
        int dw = (int)(tw * s), dh = (int)(th * s);
        if (dw < 1) dw = 1; if (dh < 1) dh = 1;
        int dx0 = box_x + (box_w - dw) / 2, dy0 = box_y + (box_h - dh) / 2;
        for (int yy = 0; yy < dh; yy++) {
            int sy = yy * th / dh;
            for (int xx = 0; xx < dw; xx++) {
                int sx = xx * tw / dw;
                uint8_t* px = &img.pixels[(sy * tw + sx) * 3];
                uint32_t col = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
                int fx = dx0 + xx, fy = dy0 + yy;
                if (fx < 0 || fy < 0 || fx >= fbw) continue;
                fb[fy * fbw + fx] = col;
            }
        }
        return 0;
    }
    case EIGEN_SETTINGS_WALL_THUMB_RAW: {
        /* (i, dst) — copy the 96x54 RGB thumbnail pixels to user memory */
        if (!user_ptr_ok(b)) return EIGEN_ERR_INVAL;
        bmp_image_t img;
        if (!wallpaper_mgr_get_thumb((int)a, &img)) return EIGEN_ERR_NOENT;
        if (!img.pixels || img.width <= 0 || img.height <= 0) return EIGEN_ERR_NOENT;
        size_t n = (size_t)img.width * (size_t)img.height * 3;
        memcpy((void*)(uintptr_t)b, img.pixels, n);
        return 0;
    }
    case EIGEN_SETTINGS_WALL_RESCAN:
        wallpaper_mgr_rescan();
        return 0;
    case EIGEN_SETTINGS_WALL_MODE:
        return (uint64_t)wallpaper_mgr_mode();
    case EIGEN_SETTINGS_WALL_PROC_ID:
        return (uint64_t)wallpaper_mgr_procedural_id();
    case EIGEN_SETTINGS_WALL_PATH: {
        if (!user_ptr_ok(a)) return EIGEN_ERR_INVAL;
        wallpaper_mgr_active_path((char*)(uintptr_t)a, (int)b);
        return 0;
    }
    case EIGEN_SETTINGS_DESKTOP_MASK:
        get_personalization()->desktop_icons_mask = a;
        return 0;
    case EIGEN_SETTINGS_DESKTOP_GET:
        return get_personalization()->desktop_icons_mask;
    case EIGEN_SETTINGS_THEME_ACTIVE:
        return (uint64_t)theme_current();
    default:
        return EIGEN_ERR_INVAL;
    }
}

extern int sys_socket(int domain, int type, int protocol);
extern int sys_connect(int s, const struct sockaddr* name, int namelen);
extern int sys_send(int s, const void* msg, int len, int flags);
extern int sys_recv(int s, void* buf, int len, int flags);
extern int sys_socket_close(int s);
extern int dns_resolve(const char* hostname, uint32_t* ip_addr);
extern void net_poll(void);

static uint64_t k_sys_net(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
    case EIGEN_NET_SOCKET:
        return (uint64_t)(int64_t)sys_socket((int)a, (int)b, (int)c);
    case EIGEN_NET_CONNECT: {
        /* a = sock, b = uint32_t ip (network byte order), c = uint16_t port */
        struct sockaddr_in addr;
        addr.sin_family = 2; // AF_INET
        addr.sin_port = htons((uint16_t)c);
        addr.sin_addr.s_addr = (uint32_t)b;
        return (uint64_t)(int64_t)sys_connect((int)a, (struct sockaddr*)&addr, sizeof(addr));
    }
    case EIGEN_NET_SEND: {
        /* a = sock, b = user buf, c = len, d(implicit) */
        if (!user_ptr_ok(b)) return (uint64_t)-1;
        return (uint64_t)(int64_t)sys_send((int)a, (const void*)(uintptr_t)b, (int)c, 0);
    }
    case EIGEN_NET_RECV: {
        /* a = sock, b = user buf, c = len */
        if (!user_ptr_ok(b)) return (uint64_t)-1;
        return (uint64_t)(int64_t)sys_recv((int)a, (void*)(uintptr_t)b, (int)c, 0);
    }
    case EIGEN_NET_CLOSE:
        return (uint64_t)(int64_t)sys_socket_close((int)a);
    case EIGEN_NET_RESOLVE: {
        /* a = hostname user ptr, b = uint32_t* ip_out user ptr */
        if (!user_ptr_ok(a) || !user_ptr_ok(b)) return (uint64_t)-1;
        uint32_t ip = 0;
        int res = dns_resolve((const char*)(uintptr_t)a, &ip);
        if (res >= 0) {
            *(uint32_t*)(uintptr_t)b = ip;
            return 0;
        }
        return (uint64_t)-1;
    }
    case EIGEN_NET_POLL:
        net_poll();
        return 0;
    default:
        return (uint64_t)-1;
    }
}

/* ------------------------------------------------------------------ */
/* Threads (EIGEN_SYS_THREAD) and futex (EIGEN_SYS_FUTEX)              */
/* The kernel keeps one task slot per thread. Threads share the group's */
/* pml4 and a per-slot TLS VMA region (see task.c); the futex WAIT/WAKE */
/* pair is the only kernel support the userland sync primitives need.  */
/* ------------------------------------------------------------------ */
static uint64_t k_sys_thread(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
    case EIGEN_THREAD_CREATE:
        /* a = trampoline, b = entry, c = arg (frame rdi/rsi = b/c) */
        if (!a || a >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
        if (!b || b >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
        return (uint64_t)create_user_thread((void*)(uintptr_t)a, b, c);
    case EIGEN_THREAD_EXIT:
        thread_exit_task((int)a);           /* never returns */
        return 0;
    case EIGEN_THREAD_JOIN: {
        for (;;) {
            task_t* t = get_task_by_id((int)a);
            if (!t) return EIGEN_ERR_NOPROC;
            if (t->state == TASK_DEAD) return (uint64_t)(uint32_t)t->exit_code;
            sleep_task(10);
        }
    }
    case EIGEN_THREAD_YIELD:
        yield();
        return 0;
    default:
        return EIGEN_ERR_INVAL;
    }
}

static uint64_t k_sys_futex(uint64_t op, uint64_t addr, uint64_t val, uint64_t timeout_ms) {
    task_t* cur = get_current_task();
    if (!cur || cur->ring != TASK_RING3) return EIGEN_ERR_INVAL;
    if (!user_ptr_ok(addr)) return EIGEN_ERR_INVAL;
    switch (op) {
    case EIGEN_FUTEX_WAIT: {
        /* Register the sleep FIRST, then re-check the value: a WAKE that
           races between our check and the state write would otherwise be
           missed. Once registered, WAKE either wakes us directly, or we
           see the new value and back out without sleeping. */
        cur->futex_addr = addr;
        cur->sleep_until = timeout_ms ? (timer_get_ms() + (uint32_t)timeout_ms)
                                      : 0xFFFFFFFFFFFFFFFFULL;
        cur->state = TASK_SLEEPING;
        int v = 0;
        __builtin_memcpy(&v, (void*)(uintptr_t)addr, 4);
        if (v != (int)val) {
            cur->futex_addr = 0;
            cur->state = TASK_READY;
            return 0;
        }
        yield();
        /* Woken: WAKE cleared futex_addr. Timed out: it is still set. */
        if (cur->futex_addr == addr) { cur->futex_addr = 0; return 1; }
        return 0;
    }
    case EIGEN_FUTEX_WAKE: {
        int n = (int)val;
        if (n <= 0) n = 1;
        int woken = 0;
        int g = cur->group > 0 ? cur->group : cur->id;
        for (int i = 1; i < MAX_TASKS && woken < n; i++) {
            task_t* t = get_task_by_index(i);
            if (!t) continue;
            /* Only wake threads of the same process: a futex address is a
               per-process virtual address (TLS/sync vars live in the shared
               pml4), so a foreign process can never legitimately wait on it. */
            if (t->state != TASK_SLEEPING || t->futex_addr != addr) continue;
            if (t->group != g) continue;
            t->state = TASK_READY;
            t->futex_addr = 0;
            woken++;
        }
        return (uint64_t)woken;
    }
    default:
        return EIGEN_ERR_INVAL;
    }
}

/* ------------------------------------------------------------------ */
/* Pipe ring buffer table (defined in task/task.c). */
extern struct pipe_buf pipes[MAX_PIPES];

/* poll(2) / epoll: fd readiness. All backing objects here are
   synchronous (files), or for pipes a 4 KB ring buffer, so readiness is
   static for files/terminal and dynamic for pipes. */
static int fd_poll_revents(int fd) {
    if (fd < 0 || fd >= MAX_FDS) return POLLNVAL;
    task_t* cur = get_current_task();
    if (cur->fd_types[fd] == FD_PIPE) {
        struct pipe_buf* p = &pipes[cur->fd_pipe[fd]];
        int r = 0;
        uint32_t avail = (p->head - p->tail + PIPE_BUF_SIZE) % PIPE_BUF_SIZE;
        if (avail > 0) r |= POLLIN | POLLRDNORM;
        if (avail < (PIPE_BUF_SIZE - 1)) r |= POLLOUT | POLLWRNORM;
        if (p->writer_count == 0) r |= POLLHUP;   /* writer closed: reader sees EOF */
        return r;
    }
    if (fd == 1 || fd == 2) return POLLOUT | POLLWRNORM;
    if (cur->fd_flags[fd] > 0 || cur->fds[fd]) return POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
    return POLLNVAL;
}

#define EIGEN_POLL_MAXFD 256

static uint64_t k_sys_poll(uint64_t fds_u, uint64_t nfds_u, uint64_t timeout_u, uint64_t a4) {
    (void)a4;
    if (nfds_u > EIGEN_POLL_MAXFD) return -(int)EINVAL;
    size_t nfds = (size_t)nfds_u;
    if (nfds && !fds_u) return -(int)EINVAL;
    int timeout_ms = (int)(int64_t)timeout_u;          /* <0 = infinite, 0 = once */
    struct eigen_pollfd* u = (struct eigen_pollfd*)(uintptr_t)fds_u;
    struct eigen_pollfd local[EIGEN_POLL_MAXFD];
    for (size_t i = 0; i < nfds; i++) {
        local[i].fd = u[i].fd;
        local[i].events = u[i].events;
        local[i].revents = 0;
    }
    int64_t deadline = (timeout_ms < 0) ? -1 : (int64_t)timer_get_ms() + timeout_ms;
    int ready = 0;
    for (;;) {
        ready = 0;
        for (size_t i = 0; i < nfds; i++) {
            int fd = local[i].fd;
            if (fd < 0) { local[i].revents = 0; continue; }
            int r = fd_poll_revents(fd);
            if (r == POLLNVAL) { local[i].revents = (short)POLLNVAL; ready++; continue; }
            r &= local[i].events;
            local[i].revents = r ? (short)r : 0;
            if (r) ready++;
        }
        if (ready || timeout_ms == 0) break;
        if (deadline < 0) {
            sleep_task(10);
        } else {
            uint32_t now = timer_get_ms();
            if ((uint32_t)deadline <= now) break;
            uint32_t rem = (uint32_t)deadline - now;
            sleep_task(rem > 10 ? 10 : rem);
        }
    }
    for (size_t i = 0; i < nfds; i++) u[i].revents = local[i].revents;
    return (uint64_t)ready;
}

/* epoll: kernel-side level-triggered monitors. Entries are validated at
   ADD time and readiness is re-scanned per epoll_wait, so no event queues
   are needed. Edge-triggered mode (EPOLLET) is not supported. */
#define EIGEN_EPOLL_MAXMON 32
#define EIGEN_EPOLL_MAXENT 64
typedef struct {
    int used;
    int owner_group;
    int count;
    struct { int fd; uint32_t events; uint64_t data; } e[EIGEN_EPOLL_MAXENT];
} eigen_epoll_mon_t;
static eigen_epoll_mon_t g_epoll[EIGEN_EPOLL_MAXMON];

static int epoll_find_entry(eigen_epoll_mon_t* m, int fd) {
    for (int i = 0; i < m->count; i++) if (m->e[i].fd == fd) return i;
    return -1;
}

static uint64_t k_sys_epoll(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    switch (op) {
    case EPOLL_EPOLL_CREATE: {
        task_t* cur = get_current_task();
        if (!cur) return -(int)EINVAL;
        for (int i = 0; i < EIGEN_EPOLL_MAXMON; i++) {
            if (!g_epoll[i].used) {
                g_epoll[i].used = 1;
                g_epoll[i].owner_group = cur->group > 0 ? cur->group : cur->id;
                g_epoll[i].count = 0;
                return (uint64_t)(i + 1);
            }
        }
        return -(int)ENFILE;
    }
    case EPOLL_EPOLL_CTL: {
        int epid = (int)a, ctl_op = (int)b;
        if (epid <= 0 || epid > EIGEN_EPOLL_MAXMON || !g_epoll[epid - 1].used) return -(int)EBADF;
        eigen_epoll_mon_t* m = &g_epoll[epid - 1];
        task_t* cur = get_current_task();
        int own = cur->group > 0 ? cur->group : cur->id;
        if (m->owner_group != own) return -(int)EBADF;
        struct eigen_epoll_ctl arg;
        __builtin_memcpy(&arg, (void*)(uintptr_t)c, sizeof(arg));
        if (arg.fd < 0 || arg.fd >= MAX_FDS) return -(int)EBADF;
        int idx = epoll_find_entry(m, arg.fd);
        switch (ctl_op) {
        case 1: /* EPOLL_CTL_ADD */
            if (idx >= 0) return -(int)EEXIST;
            if (m->count >= EIGEN_EPOLL_MAXENT) return -(int)ENOSPC;
            if (fd_poll_revents(arg.fd) == POLLNVAL) return -(int)EBADF;
            m->e[m->count].fd = arg.fd;
            m->e[m->count].events = arg.events;
            m->e[m->count].data = arg.data;
            m->count++;
            return 0;
        case 2: /* EPOLL_CTL_MOD */
            if (idx < 0) return -(int)ENOENT;
            m->e[idx].events = arg.events;
            m->e[idx].data = arg.data;
            return 0;
        case 3: /* EPOLL_CTL_DEL */
            if (idx < 0) return -(int)ENOENT;
            for (int i = idx; i < m->count - 1; i++) m->e[i] = m->e[i + 1];
            m->count--;
            return 0;
        default:
            return -(int)EINVAL;
        }
    }
    case EPOLL_EPOLL_WAIT: {
        /* a = epid, b = user event array, c = (maxevents << 32) | timeout_ms */
        int epid = (int)a;
        int maxevents = (int)(c >> 32);
        int timeout_ms = (int)(int32_t)(c & 0xFFFFFFFFULL);
        if (epid <= 0 || epid > EIGEN_EPOLL_MAXMON || !g_epoll[epid - 1].used) return -(int)EBADF;
        eigen_epoll_mon_t* m = &g_epoll[epid - 1];
        task_t* cur = get_current_task();
        int own = cur->group > 0 ? cur->group : cur->id;
        if (m->owner_group != own) return -(int)EBADF;
        struct eigen_epoll_event* out = (struct eigen_epoll_event*)(uintptr_t)b;
        if (maxevents > EIGEN_EPOLL_MAXENT) maxevents = EIGEN_EPOLL_MAXENT;
    int64_t deadline = (timeout_ms < 0) ? -1 : (int64_t)timer_get_ms() + timeout_ms;
        for (;;) {
            int n = 0;
            for (int i = 0; i < m->count && n < maxevents; i++) {
                int r = fd_poll_revents(m->e[i].fd);
                if (r == POLLNVAL) r = POLLERR;
                r &= (int)m->e[i].events;
                if (r) {
                    out[n].events = (uint32_t)r;
                    out[n].data = m->e[i].data;
                    n++;
                }
            }
            if (n) return (uint64_t)n;
            if (timeout_ms == 0) return 0;
            if (deadline < 0) {
                sleep_task(10);
            } else {
                uint32_t now = timer_get_ms();
                if ((uint32_t)deadline <= now) return 0;
                uint32_t rem = (uint32_t)deadline - now;
                sleep_task(rem > 10 ? 10 : rem);
            }
        }
    }
    default:
        return -(int)EINVAL;
    }
}

static uint64_t k_sys_pipe(uint64_t pipefd_u, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    if (!user_ptr_ok(pipefd_u)) return -(int)EINVAL;
    int pfd[2];
    int r = sys_pipe(pfd);
    if (r < 0) return -(int)EMFILE;
    int* out = (int*)(uintptr_t)pipefd_u;
    out[0] = pfd[0]; out[1] = pfd[1];
    return 0;
}

static uint64_t k_sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg, uint64_t a4) {
    (void)a4;
    int r = sys_fcntl((int)fd, (int)cmd, (int)arg);
    if (r < 0) return -(int)EBADF;
    return (uint64_t)r;
}

static syscall_fn syscall_table[SYSCALL_COUNT] = {
    k_sys_open,         // 0
    k_sys_write,        // 1
    k_sys_read,         // 2
    k_sys_close,        // 3
    k_sys_getpid,       // 4
    k_sys_sleep,        // 5
    k_sys_gettime,      // 6
    k_sys_alloc,        // 7
    k_sys_free,         // 8
    k_sys_exit,         // 9
    k_sys_spawn,        // 10
    k_sys_wait,         // 11
    k_sys_sysinfo,      // 12
    k_sys_gfx,          // 13
    k_sys_input,        // 14
    k_sys_beep,         // 15
    k_sys_win,          // 16
    k_sys_time,         // 17
    k_sys_fontmap,      // 18
    k_sys_modload,      // 19
    k_sys_fs,           // 20
    k_sys_gettheme,     // 21
    k_sys_lseek,        // 22
    k_sys_settings,     // 23
    k_sys_net,          // 24
    k_sys_thread,       // 25
    k_sys_futex,        // 26
    k_sys_poll,         // 27
    k_sys_epoll,        // 28
    k_sys_pipe,         // 29
    k_sys_fcntl         // 30
};

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    if (num >= SYSCALL_COUNT) return -1;
    syscall_fn fn = syscall_table[num];
    if (!fn) return -1;
    return fn(arg1, arg2, arg3, arg4);
}