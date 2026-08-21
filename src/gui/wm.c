/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "gui/wm.h"
#include "gui/gui.h"
#include "gui/gui_anim.h"
extern const uint8_t font8x16[];   /* defined in drivers/video/framebuffer.c */
#include "handler/error.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard.h"
#include "kernel/time/timer.h"
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include "kernel/task/task.h"
#include "filesystem/filesystem.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

static void wm_save_anim_cfg(void);

extern void draw_premium_wallpaper(void);
extern void draw_taskbar(void);

#define MAX_BUTTONS_PER_WINDOW 512
#define DESKTOP_COUNT 5

static wm_window_t windows[WM_MAX_WINDOWS];
static int window_count = 0;
static int focused_id = -1;
static int next_win_id = 1;

/* ── Tiling window manager ────────────────────────────────────
   When enabled, all non-floating windows are laid out in a
   master + stack grid inside the work area so they never overlap. */
static int g_tile_enabled = 0;
void wm_set_tiling(int on) { g_tile_enabled = on ? 1 : 0; }
int  wm_get_tiling(void)   { return g_tile_enabled; }

/* defined further below; used by wm_tile_all */
static void fix_after_tile(wm_window_t* w);

/* theme-aware title-bar button: roundness + classic top highlight */
static void draw_tb_btn(int bx, int by, int bs, uint32_t bg, uint32_t fg,
                        wm_theme_params_t* tp, int classic_top) {
    theme_id_t cur_theme = theme_current();
    if (tp && tp->btn_style > 0) {
        int rr = (tp->btn_style == 1) ? (bs/4) : 2;
        gfx_fill_rect_rounded(bx, by, bs, bs, rr, bg);
        gfx_draw_rect_rounded_outline(bx, by, bs, bs, rr, 1, 0x000000);
    } else {
        gfx_fill_rect(bx, by, bs, bs, bg);
        gfx_draw_rect_outline(bx, by, bs, bs, 1, 0x000000);
    }
    if (classic_top && tp && tp->btn_style == 2) {
        gfx_draw_hline(bx + 1, by + 1, bs - 2, gfx_lighten(bg, 40));
    }
    (void)fg;
    (void)cur_theme;
}
static int prev_mouse_btn = 0;
static int prev_mx = -1, prev_my = -1;

/* Unified per-frame mouse snapshot: computed once at the start of wm_tick
   so every app render callback reads the exact same input state (position,
   held buttons, press/release edges) instead of each app polling the live
   mouse and tracking its own prev-button edge. */
static int snap_mx = 0, snap_my = 0;
static int snap_mbtn = 0;     /* live button state this frame */
static int snap_lc = 0;       /* left  press edge */
static int snap_lr = 0;       /* left  release edge */
static int snap_rc = 0;       /* right press edge */
static int snap_rr = 0;       /* right release edge */
static int snap_ac = 0;       /* any   press edge */
static int snap_ar = 0;       /* any   release edge */

int wm_mouse_x(void)         { return snap_mx; }
int wm_mouse_y(void)         { return snap_my; }
int wm_mouse_buttons(void)   { return snap_mbtn; }
int wm_mouse_left_clicked(void)   { return snap_lc; }
int wm_mouse_left_released(void)  { return snap_lr; }
int wm_mouse_left_held(void)      { return !!(snap_mbtn & 1); }
int wm_mouse_right_clicked(void)  { return snap_rc; }
int wm_mouse_right_released(void) { return snap_rr; }
int wm_mouse_right_held(void)     { return !!(snap_mbtn & 2); }
int wm_mouse_any_clicked(void)    { return snap_ac; }
int wm_mouse_any_released(void)   { return snap_ar; }
static int current_desktop = 0;
static int desktop_prev[DESKTOP_COUNT] = {0, 0, 0, 0, 0};

/* ── Window open-animation (global selection) ──────────────── */
static int g_open_anim = WM_ANIM_ZOOM;   /* default: zoom-in on open */
#define OPEN_ANIM_MS 300
#define GEOM_ANIM_MS 220   /* maximize / restore / minimize tween duration */

#define TOP_BAR_H 36

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

void wm_init(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].id = -1;
        windows[i].flags = 0;
        windows[i].button_count = 0;
    }
    for (int i = 0; i < DESKTOP_COUNT; i++) desktop_prev[i] = 0;
}

/* ── Custom user shortcuts (cfg/shortcuts.cfg) ───────────────
   Moved here from the old Keyboard Shortcuts app: the dispatch is
   global, so it must live in the window manager. Settings → Shortcuts
   is a Coming Soon section; these load at boot and stay live. */
#define KS_MAX       24
#define KS_MOD_SUPER 1
#define KS_MOD_CTRL  2
#define KS_MOD_ALT   4
#define KS_MOD_SHIFT 8

typedef struct {
    int  used;
    int  mods;     /* KS_MOD_* bits */
    int  key;      /* lower-case ascii key */
    int  target;   /* global_idx of app to launch */
} ks_custom_t;

static ks_custom_t g_ks[KS_MAX];

static void ks_load(void) {
    for (int i = 0; i < KS_MAX; i++) g_ks[i].used = 0;
    int fd = fs_open("cfg/shortcuts.cfg", 0);
    if (fd < 0) return;
    char buf[512]; int r = fs_read(fd, buf, (int)sizeof(buf) - 1); fs_close(fd);
    if (r <= 0) return;
    buf[r] = 0;
    int i = 0;
    for (int p = 0; p < r && i < KS_MAX; ) {
        int e = p; while (e < r && buf[e] != '\n') e++;
        int line = (e < r) ? e - p : e - p;
        int m = 0, k = 0, t = 0, cnt = 0;
        for (int q = 0; q < line && cnt < 3; ) {
            while (q < line && (buf[p+q]==' '||buf[p+q]=='\t')) q++;
            if (q >= line) break;
            int acc = 0, got = 0;
            while (p+q<e && buf[p+q]>='0'&&buf[p+q]<='9'){acc=acc*10+(buf[p+q]-'0');q++;got=1;}
            if (got){ if(cnt==0)m=acc; else if(cnt==1)k=acc; else t=acc; cnt++; }
            while (p+q<e && buf[p+q]!=' ' && buf[p+q]!='\t') q++;
        }
        if (cnt >= 3) { g_ks[i].used = 1; g_ks[i].mods = m; g_ks[i].key = k; g_ks[i].target = t; i++; }
        p = (e < r) ? e + 1 : r + 1;
    }
}

void custom_shortcut_on_key(int key, int mods) {
    static int loaded = 0;
    if (!loaded) { ks_load(); loaded = 1; }
    if (key == 0) return;
    for (int i = 0; i < KS_MAX; i++) {
        if (g_ks[i].used && g_ks[i].key == key && g_ks[i].mods == mods) {
            launch_item(g_ks[i].target);
            return;
        }
    }
}

static wm_window_t* find_window(int id) {
    if (id < 0) return 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id == id) return &windows[i];
    }
    return 0;
}

static void bring_to_front(wm_window_t* win) {
    if (!win) return;
    int max_z = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id != -1 && (windows[i].flags & WM_FLAG_VISIBLE)) {
            if (windows[i].z_order > max_z) max_z = windows[i].z_order;
            windows[i].flags &= ~WM_FLAG_FOCUSED;
        }
    }
    win->z_order = max_z + 1;
    win->flags |= WM_FLAG_FOCUSED;
    focused_id = win->id;
}

int wm_open_window(int x, int y, int w, int h, const char* title, uint32_t accent,
                   wm_render_cb on_render,
                   wm_key_cb on_key,
                   wm_resize_cb on_resize) {
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id == -1) { slot = i; break; }
    }
    if (slot == -1) return -1;

    int wx = 0, wy = 0, ww = (int)get_fb_width(), wh = (int)get_fb_height();
    gui_get_work_area(&wx, &wy, &ww, &wh);
    if (wh > 0) {
        int max_h = wh - 8;
        if (h > max_h) h = max_h;
        if (y + h > wy + wh - 4) {
            y = wy + wh - 4 - h;
        }
        if (y < wy) y = wy;
        if (x < wx) x = wx;
        if (x + w > wx + ww) x = wx + ww - w;
    }

    wm_window_t* win = &windows[slot];
        win->id = next_win_id++;
    win->x = x; if (win->x < 0) win->x = 0;
    win->y = y;
    win->w = w; win->h = h;
    win->desktop = current_desktop;
    win->view_x = 0; win->view_y = 0;
    win->content_w = w; win->content_h = h - WM_TITLEBAR_H;
    /* Initialise restored-geometry so maximize/minimize have correct fallback */
    win->saved_x = x; win->saved_y = y;
    win->saved_w = w; win->saved_h = h;
    int j = 0; while (title[j] && j < 63) { win->title[j] = title[j]; j++; } win->title[j] = 0;
    win->accent_color = accent;
    win->flags = WM_FLAG_VISIBLE | WM_FLAG_CLOSABLE;
    win->on_render = on_render;
    win->on_key = on_key;
    win->on_resize = on_resize;
    win->button_count = 0;
    win->app_data = 0;
    win->last_title_click_ms = 0;
    win->title_click_x = 0; win->title_click_y = 0;
    win->open_ms = timer_get_ms();
    win->anim_type = g_open_anim;

    bring_to_front(win);
    window_count++;
    if (g_tile_enabled) wm_tile_all();
    return win->id;
}

int wm_window_count(void) { return window_count; }

/* ── Ring-3 user windows ─────────────────────────────────────
   A user window owns a content buffer (kernel heap). The buffer is
   aliased into the creating task's address space (VMM_USER) so the
   app draws into it directly; the WM composites it into the back
   buffer during the render pass. Input and redraw requests arrive
   as queue events instead of C callbacks. */

#define USER_WIN_VMA_BASE    0x80000000ULL
#define USER_WIN_VMA_STRIDE  0x400000ULL   /* 4 MB per slot */
#define USER_WIN_MAX_BUF     0x400000      /* 4 MB (1024x1024) */

static uint64_t wm_user_win_vma(int slot) {
    return USER_WIN_VMA_BASE + (uint64_t)slot * USER_WIN_VMA_STRIDE;
}

static void wm_user_free_buffer(wm_window_t* win) {
    if (win->user_buf) {
        kfree_aligned(win->user_buf);
        win->user_buf = 0;
    }
    if (win->user_cbuf) {
        kfree_aligned(win->user_cbuf);
        win->user_cbuf = 0;
    }
    if (win->user_old_buf) {
        kfree(win->user_old_buf);
        win->user_old_buf = 0;
    }
    if (win->user_old_cbuf) {
        kfree(win->user_old_cbuf);
        win->user_old_cbuf = 0;
    }
    win->user_buf_w = win->user_buf_h = 0;
}

/* Request a content-size change for a user window. The WM frame must NOT
   touch user_buf_w/h directly: the app is mid-frame and reads those via
   eigen_win_getsize() every frame, so changing them mid-frame makes the
   app flush the wrong byte count (garbage strips on grow, cut-off frames
   on shrink). Instead we mark the request and apply it atomically inside
   wm_user_flush(), which runs between two of the app's frames.
   Buffers are a FIXED 4 MB allocation (USER_WIN_MAX_BUF) mapped once at
   creation, so applying a resize never remaps: we only update the logical
   size, preserve the top-left content and clear newly-exposed pixels. */
static void wm_user_mark_resize(wm_window_t* win, int cw, int ch) {
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    uint64_t max_px = USER_WIN_MAX_BUF / 4;
    uint64_t new_px = (uint64_t)cw * ch;
    if (new_px > max_px) {
        int f = (int)((new_px + max_px - 1) / max_px);
        while (f > 1 && ((uint64_t)cw / f) * ((uint64_t)ch / f) > max_px) f--;
        if (f < 1) f = 1;
        cw /= f; ch /= f;
        if (cw < 1) cw = 1; if (ch < 1) ch = 1;
    }
    win->user_pending_w = cw;
    win->user_pending_h = ch;
}

int wm_user_enqueue(int id, uint32_t type, int a, int b, int c, int d) {
    wm_window_t* win = find_window(id);
    if (!win || !(win->flags & WM_FLAG_USER)) return -1;
    uint32_t next = (win->user_ev_tail + 1) % WM_USER_EV_QMAX;
    if (next == win->user_ev_head) return -1;   /* queue full */
    wm_user_ev_t* e = &win->user_ev_q[win->user_ev_tail];
    e->type = type; e->a = a; e->b = b; e->c = c; e->d = d;
    win->user_ev_tail = next;
    return 0;
}

int wm_user_poll(int id, wm_user_ev_t* out, int max) {
    wm_window_t* win = find_window(id);
    if (!win || !out || max <= 0) return -1;
    if (!(win->flags & WM_FLAG_USER)) return -1;
    int n = 0;
    while (win->user_ev_head != win->user_ev_tail && n < max) {
        out[n++] = win->user_ev_q[win->user_ev_head];
        win->user_ev_head = (win->user_ev_head + 1) % WM_USER_EV_QMAX;
    }
    return n;
}

int wm_user_flush(int id) {
    wm_window_t* win = find_window(id);
    if (!win || !(win->flags & WM_FLAG_USER) || !win->user_buf || !win->user_cbuf)
        return -1;
    /* Commit the app's latest frame into the stable composite buffer.
       The WM composites user_cbuf, so it never reads a partially drawn
       frame — this is what removes the ring-3 flicker/tearing. */
    uint64_t n = (uint64_t)win->user_buf_w * win->user_buf_h;
    uint32_t* src = win->user_buf;
    uint32_t* dst = win->user_cbuf;
    if (!(win->flags & WM_FLAG_INSTRUMENTED)) {
        win->flags |= WM_FLAG_INSTRUMENTED;
        extern void serial_puts(const char* s);
        extern void serial_u64(uint64_t v);
        serial_puts("[W] first flush "); serial_u64(n); serial_puts(" px at "); serial_u64((uint64_t)timer_get_ms()); serial_puts(" ms\n");
    }
    for (uint64_t i = 0; i < n; i++) dst[i] = src[i];
    /* Apply a pending resize now: the app has finished this frame with the
       current user_buf_w/h, so changing them here is race-free. The app
       reads the new size at its next frame start. Keep the top-left content
       intact and clear pixels newly exposed by a grow. */
    if (win->user_pending_w > 0 && win->user_pending_h > 0 &&
        (win->user_pending_w != win->user_buf_w ||
         win->user_pending_h != win->user_buf_h)) {
        uint64_t old_px = n;
        uint64_t new_px = (uint64_t)win->user_pending_w * win->user_pending_h;
        win->user_buf_w = win->user_pending_w;
        win->user_buf_h = win->user_pending_h;
        win->user_pending_w = win->user_pending_h = 0;
        if (new_px > old_px) {
            for (uint64_t i = old_px; i < new_px; i++) {
                src[i] = 0x0B0E14;
                dst[i] = 0x0B0E14;
            }
        }
    }
    /* A resize may have remapped the VMA mid-frame: the app has now
       finished drawing with the previous frame's dimensions, so the
       pre-resize buffers can be reclaimed. */
    if (win->user_old_buf) { kfree(win->user_old_buf); win->user_old_buf = 0; }
    if (win->user_old_cbuf) { kfree(win->user_old_cbuf); win->user_old_cbuf = 0; }
    return 0;
}

int wm_open_window_user(int x, int y, int w, int h, const char* title, uint32_t accent) {
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id == -1) { slot = i; break; }
    }
    if (slot == -1) return -1;

    int wx = 0, wy = 0, ww = (int)get_fb_width(), wh = (int)get_fb_height();
    gui_get_work_area(&wx, &wy, &ww, &wh);
    if (wh > 0) {
        int max_h = wh - 8;
        if (h > max_h) h = max_h;
        if (y + h > wy + wh - 4) y = wy + wh - 4 - h;
        if (y < wy) y = wy;
        if (x < wx) x = wx;
        if (x + w > wx + ww) x = wx + ww - w;
    }

    /* Content buffer dimensions = the visible content area INSIDE the chrome.
       cx = wx+1 (1px left border), cw = ww-2 (both borders),
       cy = wy+titlebar+1, ch = wh-titlebar-2.
       Using bw=w (full window width) caused the right-edge pixel of every
       row to map into the NEXT row (stride = sw_px = w, but blit writes
       only cw = w-2 pixels). That shifted addr wraps and produces the
       mirror effect. Matching bw/bh to cw/ch means sw_px == cw and the
       steady-state 1:1 blit is always taken — no stretch math, no wrap. */
    int bw = w - 2,  bh = h - WM_TITLEBAR_H - 2;
    if ((uint64_t)bw * bh * 4 > USER_WIN_MAX_BUF) {
        /* scale the content down to fit the 4 MB budget */
        uint64_t max_px = USER_WIN_MAX_BUF / 4;
        uint64_t area = (uint64_t)bw * bh;
        if (area > max_px) {
            int f = (int)((area + max_px - 1) / max_px);
            while (f > 1 && ((uint64_t)bw / f) * ((uint64_t)bh / f) > max_px) f--;
            if (f < 1) f = 1;
            bw /= f; bh /= f;
        }
    }
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;

    /* Allocate the FULL slot (USER_WIN_MAX_BUF = 4 MB) once. The per-slot
       VMA (USER_WIN_VMA_STRIDE = 4 MB) is mapped to cover the whole region,
       so any content size (clamped to the budget) lives inside the same
       already-mapped physical pages. Resizes then only change the logical
       user_buf_w/h — they NEVER reallocate or remap. This is deliberate:
       vmm_map_range() maps into active_pml4(), which during a WM/kernel
       operation is the KERNEL pml4, not the owning ring-3 app's pml4. A
       late remap therefore never reaches the app's address space, leaving
       the app's eigen_win_map() pointing at stale/garbage pages while
       eigen_win_getsize() reports the new size — the classic "distorted then
       crashes on resize/maximize" bug. Allocating per-slot up front removes
       the remap path and the crash entirely. */
    uint64_t cap_px = USER_WIN_MAX_BUF / 4;
    if ((uint64_t)bw * bh > cap_px) {
        /* requested content exceeds the 4 MB budget: scale it down (mirrors
           the clamp used on grow) so it still fits the fixed slot. */
        uint64_t area = (uint64_t)bw * bh;
        int f = (int)((area + cap_px - 1) / cap_px);
        while (f > 1 && ((uint64_t)bw / f) * ((uint64_t)bh / f) > cap_px) f--;
        if (f < 1) f = 1;
        bw /= f; bh /= f;
    }

    /* Page-align the buffers: the per-slot VMA maps each 4KB page with
       vmm_get_phys(buf+off) masked to a page boundary. If buf is not page
       aligned, VMA base maps to buf - (buf&0xFFF) — i.e. the kmalloc block
       header 24 bytes before the payload — and the app's first paint clobbers
       that header (marking the block free), so the allocator can later hand the
       same address out as a second task's kernel stack. Aligning buf makes the
       VMA map start exactly at the payload. */
    uint32_t* buf = (uint32_t*)kmalloc_aligned(USER_WIN_MAX_BUF, 4096);
    if (!buf) return -1;

    /* Composite buffer: a stable frame the WM composites from. The app
       draws into `buf`; flush copies buf→cbuf so the WM never reads a
       half-updated frame (no tearing/flicker). */
    uint32_t* cbuf = (uint32_t*)kmalloc_aligned(USER_WIN_MAX_BUF, 4096);
    if (!cbuf) { kfree_aligned(buf); return -1; }
    /* Start blank (window bg-ish) so there is no garbage flash before the
       first flush. The app's first frame overwrites it. */
    for (uint64_t i = 0; i < cap_px; i++) { buf[i] = 0x0B0E14; cbuf[i] = 0x0B0E14; }

    wm_window_t* win = &windows[slot];
    win->id = next_win_id++;
    win->x = x; if (win->x < 0) win->x = 0;
    win->y = y;
    win->w = w; win->h = h;
    win->desktop = current_desktop;
    win->view_x = 0; win->view_y = 0;
    win->content_w = bw; win->content_h = bh;
    win->saved_x = x; win->saved_y = y;
    win->saved_w = w; win->saved_h = h;
    int j = 0; while (title[j] && j < 63) { win->title[j] = title[j]; j++; } win->title[j] = 0;
    win->accent_color = accent;
    win->flags = WM_FLAG_VISIBLE | WM_FLAG_CLOSABLE | WM_FLAG_USER;
    win->on_render = 0;
    win->on_key = 0;
    win->on_resize = 0;
    win->button_count = 0;
    win->app_data = 0;
    win->last_title_click_ms = 0;
    win->title_click_x = 0; win->title_click_y = 0;
    win->open_ms = timer_get_ms();
    win->anim_type = g_open_anim;

    win->user_buf = buf;
    win->user_cbuf = cbuf;
    win->user_buf_w = bw;
    win->user_buf_h = bh;
    win->user_pid = get_current_task_id();
    win->user_ev_head = win->user_ev_tail = 0;
    win->user_lmx = win->user_lmy = -1;

    /* Alias the buffer into the current task's address space (the syscall
       runs in the app's pml4): map every page individually with USER
       permission so the ring3 app can draw directly. kmalloc pages are not
       guaranteed to be physically contiguous, so each 4KB page is translated
       via vmm_get_phys and mapped into the slot's VMA range. */
    for (uint64_t off = 0; off < USER_WIN_MAX_BUF; off += 4096) {
        uint64_t page_phys = vmm_get_phys((uint64_t)(uintptr_t)buf + off);
        if (page_phys) {
            vmm_map_range(wm_user_win_vma(slot) + off, page_phys & ~0xFFFULL,
                          4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
        }
    }

    bring_to_front(win);
    window_count++;
    wm_user_enqueue(win->id, WM_USER_EV_RENDER, 0, 0, 0, 0);
    if (g_tile_enabled) wm_tile_all();
    return win->id;
}

void* wm_user_map_buffer(int id) {
    wm_window_t* win = find_window(id);
    if (!win || !win->user_buf) return 0;
    if (!(win->flags & WM_FLAG_USER)) return 0;
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (&windows[i] == win) { slot = i; break; }
    }
    if (slot < 0) return 0;
    return (void*)(uintptr_t)wm_user_win_vma(slot);
}

/* Debug: dump every ring-3 window's heap buffer addresses + owner pid so a
   crash dump can tell whether a faulting kernel stack overlaps one of them.
   Returns the number of user windows found. */
int wm_dump_user_windows(uint64_t* ids, uint64_t* bufs, uint64_t* cbufs, int* pids, int max) {
    int n = 0;
    for (int i = 0; i < WM_MAX_WINDOWS && n < max; i++) {
        wm_window_t* w = &windows[i];
        if (w->id == -1 || !(w->flags & WM_FLAG_USER)) continue;
        ids[n] = (uint64_t)w->id;
        bufs[n] = (uint64_t)(uintptr_t)w->user_buf;
        cbufs[n] = (uint64_t)(uintptr_t)w->user_cbuf;
        pids[n] = w->user_pid;
        n++;
    }
    return n;
}

/* ── Tiling layout ───────────────────────────────────────────
   Arrange every non-floating, non-minimized window inside the
   work area using a master + stack split. Guarantees no overlap.
   Windows explicitly marked floating (flags & WM_FLAG_FLOATING)
   are left where they are. */
#define WM_FLAG_FLOATING 0x800
void wm_tile_all(void) {
    int wx, wy, ww, wh;
    wx = 0; wy = 0; ww = (int)get_fb_width(); wh = (int)get_fb_height();
    gui_get_work_area(&wx, &wy, &ww, &wh);

    /* collect tiled (non-floating, non-minimized) windows */
    int ids[WM_MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* w = &windows[i];
        if (!(w->flags & WM_FLAG_VISIBLE)) continue;
        if (w->flags & WM_FLAG_MINIMIZED) continue;
        if (w->flags & WM_FLAG_FLOATING) continue;
        ids[n++] = w->id;
    }
    if (n == 0) return;

    int gap = 4;
    int ax = wx + gap, ay = wy + gap;
    int aw = ww - gap * 2, ah = wh - gap * 2;
    if (aw < 80) aw = 80; if (ah < 80) ah = 80;

    if (n == 1) {
        wm_window_t* w = find_window(ids[0]);
        w->x = ax; w->y = ay; w->w = aw; w->h = ah;
        fix_after_tile(w);
        return;
    }

    /* master on the left (50%), stack on the right split horizontally */
    int master_w = aw * 55 / 100;
    int stack_x = ax + master_w + gap;
    int stack_w = aw - master_w - gap;
    int stack_h = (ah - gap * (n - 1)) / n;

    wm_window_t* m = find_window(ids[0]);
    m->x = ax; m->y = ay; m->w = master_w; m->h = ah;
    fix_after_tile(m);

    for (int i = 1; i < n; i++) {
        wm_window_t* w = find_window(ids[i]);
        w->x = stack_x;
        w->y = ay + (i - 1) * (stack_h + gap);
        w->w = stack_w;
        w->h = stack_h;
        fix_after_tile(w);
    }
}

/* clamp/restore geometry after a tiled move (keeps content + saved geom in sync) */
static void fix_after_tile(wm_window_t* w) {
    if (!w) return;
    if (w->w < WM_MIN_W) w->w = WM_MIN_W;
    if (w->h < WM_MIN_H) w->h = WM_MIN_H;
    w->content_w = w->w;
    w->content_h = w->h - WM_TITLEBAR_H;
    w->saved_x = w->x; w->saved_y = w->y;
    w->saved_w = w->w; w->saved_h = w->h;
    /* User windows must resize with the tile or their buffer stride stays at
       the pre-tile width while the WM blits at the new width — the 1:1 blit
       then wraps rows and mirrors the app's content. Mark the resize so the
       next flush applies it atomically between the app's frames. */
    if (w->flags & WM_FLAG_USER)
        wm_user_mark_resize(w, w->w - 2, w->h - WM_TITLEBAR_H - 2);
    if (w->on_resize) w->on_resize(w->id, w->w, w->h);
}

void wm_set_open_anim(int type) {
    if (type < WM_ANIM_NONE || type > WM_ANIM_DROP) type = WM_ANIM_ZOOM;
    g_open_anim = type;
    wm_save_anim_cfg();
}

int wm_get_open_anim(void) { return g_open_anim; }

/* Persist the open-animation choice to cfg/anim.cfg and restore it. */
void wm_load_anim_cfg(void) {
    fs_mkdir("cfg");
    int fd = fs_open("cfg/anim.cfg", 0);
    if (fd >= 0) {
        char buf[16]; int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n > 0) { buf[n] = 0; int v = atoi(buf); wm_set_open_anim(v); }
    }
}

static void wm_save_anim_cfg(void) {
    fs_mkdir("cfg");
    fs_delete("cfg/anim.cfg");
    int fd = fs_create("cfg/anim.cfg");
    if (fd >= 0) {
        char buf[8]; int n = snprintf(buf, sizeof(buf), "%d", g_open_anim);
        fs_write(fd, buf, n); fs_close(fd);
    }
}

/* Begin a geometry tween from the window's CURRENT rect to (tx,ty,tw,th).
   The window's logical rect is updated immediately (so hit-testing and
   the app see the final size), while render_window interpolates the
   visible rect until GEOM_ANIM_MS elapses. */
static void start_geom_tween(wm_window_t* win, int tx, int ty, int tw, int th, int kind) {
    win->anim_from_x = win->x; win->anim_from_y = win->y;
    win->anim_from_w = win->w; win->anim_from_h = win->h;
    win->anim_to_x = tx; win->anim_to_y = ty;
    win->anim_to_w = tw; win->anim_to_h = th;
    win->geom_anim_ms = timer_get_ms();
    win->geom_anim = kind;
    win->x = tx; win->y = ty; win->w = tw; win->h = th;
}

void wm_close_window(int id) {
    wm_window_t* win = find_window(id);
    if (!win) return;
    if (win->flags & WM_FLAG_CLOSING) return;   /* already animating out */
    if (win->anim_type != WM_ANIM_NONE && !(win->flags & (WM_FLAG_MAXIMIZED|WM_FLAG_MAX_H|WM_FLAG_MAX_V))) {
        /* play a short close animation; actual free is deferred in wm_tick */
        win->flags |= WM_FLAG_CLOSING;
        win->close_ms = timer_get_ms();
    } else {
        wm_user_free_buffer(win);
        win->id = -1;
        win->flags = 0;
        window_count--;
        if (focused_id == id) focused_id = -1;
    }
}

/* Close any ring-3 window whose owning task has died. A dead app's window
   otherwise stays open forever, showing its last (possibly blank) frame. */
void wm_user_cleanup_dead(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* win = &windows[i];
        if (win->id == -1 || !(win->flags & WM_FLAG_USER)) continue;
        if (win->user_pid <= 0) continue;
        task_t* t = get_task_by_id(win->user_pid);
        if (!t || t->state == TASK_DEAD || t->state == TASK_FREE)
            wm_close_window(win->id);
    }
}

/* Blit 8x16 text into a raw window buffer (used for the crash screen). */
static void wm_blit_text(uint32_t* buf, int w, int h, int x, int y,
                         const char* s, uint32_t rgb) {
    for (int i = 0; s[i]; i++) {
        const uint8_t* g = &font8x16[(unsigned char)s[i] * 16];
        for (int row = 0; row < 16; row++)
            for (int bit = 0; bit < 8; bit++)
                if (g[row] & (0x80 >> bit)) {
                    int px = x + i * 8 + bit, py = y + row;
                    if (px >= 0 && py >= 0 && px < w && py < h)
                        buf[py * w + px] = rgb;
                }
    }
}

/* Paint a crash report into a user window (called from the fault handler
   when a ring-3 task faults). The window stays open so the failure is
   visible; the user closes it manually. */
void wm_mark_crashed(int pid, uint64_t rip, uint64_t fault, uint64_t err) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* win = &windows[i];
        if (win->id == -1 || !(win->flags & WM_FLAG_USER)) continue;
        if (win->user_pid != pid) continue;
        win->user_pid = 0;   /* don't auto-close: show the crash screen */
        uint32_t* buf = win->user_cbuf;
        int w = win->user_buf_w, h = win->user_buf_h;
        if (!buf || w <= 0 || h <= 0) continue;
        for (uint64_t p = 0; p < (uint64_t)w * (uint64_t)h; p++) buf[p] = 0x100808;
        char line[80];
        wm_blit_text(buf, w, h, 24, h / 2 - 40, "APP CRASHED (ring-3 fault)", 0xFF6666);
        wm_blit_text(buf, w, h, 24, h / 2 - 16,
                     "The task was killed; the OS is fine.", 0xCCCCCC);
        snprintf(line, sizeof(line), "RIP=0x%llX  FAULT=0x%llX  ERR=%llu",
                 (unsigned long long)rip, (unsigned long long)fault,
                 (unsigned long long)err);
        wm_blit_text(buf, w, h, 24, h / 2 + 8, line, 0xAAAAAA);
        wm_blit_text(buf, w, h, 24, h / 2 + 32, "Close this window.", 0x888888);
    }
}

void wm_add_button(int win_id, int btn_id, int x, int y, int w, int h, const char* label,
                  uint32_t bg, uint32_t fg, void (*on_click)(int, int)) {
    wm_window_t* win = find_window(win_id);
    if (!win || win->button_count >= MAX_BUTTONS_PER_WINDOW) return;
    int b = win->button_count++;
    win->buttons[b].id = btn_id;
    win->buttons[b].x = x; win->buttons[b].y = y;
    win->buttons[b].w = w; win->buttons[b].h = h;
    int j = 0; while (label[j] && j < 31) { win->buttons[b].label[j] = label[j]; j++; } win->buttons[b].label[j] = 0;
    win->buttons[b].bg_color = bg;
    win->buttons[b].fg_color = fg;
    win->buttons[b].on_click = on_click;
    win->buttons[b].is_hovered = 0;
    win->buttons[b].is_active = 1;
}

void wm_clear_buttons(int win_id) {
    wm_window_t* win = find_window(win_id);
    if (win) win->button_count = 0;
}

void wm_set_button_active(int win_id, int btn_id, int active) {
    wm_window_t* win = find_window(win_id);
    if (!win) return;
    for (int i = 0; i < win->button_count; i++) {
        if (win->buttons[i].id == btn_id) { win->buttons[i].is_active = active; break; }
    }
}

static void get_sorted_windows(wm_window_t** sorted, int* count) {
    *count = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id != -1 && (windows[i].flags & WM_FLAG_VISIBLE) && windows[i].desktop == current_desktop) {
            sorted[(*count)++] = &windows[i];
        }
    }
    for (int i = 0; i < *count - 1; i++) {
        for (int j = 0; j < *count - i - 1; j++) {
            if (sorted[j]->z_order > sorted[j+1]->z_order) {
                wm_window_t* tmp = sorted[j]; sorted[j] = sorted[j+1]; sorted[j+1] = tmp;
            }
        }
    }
}

static wm_window_t* window_at_point(int px, int py) {
    wm_window_t* sorted[WM_MAX_WINDOWS];
    int count;
    get_sorted_windows(sorted, &count);
    for (int i = count - 1; i >= 0; i--) {
        if (point_in_rect(px, py, sorted[i]->x, sorted[i]->y, sorted[i]->w, sorted[i]->h)) return sorted[i];
    }
    return 0;
}

/* Composite a ring-3 window's content buffer into the back buffer.
   The dest rect is the window content area (wx, wy, ww, wh are the FULL
   window rect including chrome, passed from render_window).
   bw/bh of the user buffer now match cw/ch exactly (wm_open_window_user
   sets bw = w-2, bh = h-titlebar-2), so the 1:1 path is the normal case.
   The stretch path fires ONLY while geom_anim or open/close animation is
   active — it scales the fixed-size buffer into the animated window area. */
static void wm_blit_user_buffer(wm_window_t* win, int wx, int wy, int ww, int wh) {
    /* Content area inside chrome (1px border each side) */
    int cx = wx + 1, cy = wy + WM_TITLEBAR_H + 1;
    int cw = ww - 2, ch = wh - WM_TITLEBAR_H - 2;
    int fw = (int)get_fb_width(), fh = (int)get_fb_height();
    extern uint32_t* gfx_get_back_buffer(void);
    uint32_t* bb = gfx_get_back_buffer();

    uint32_t* src = win->user_cbuf;
    int sw_px = win->user_buf_w, sh_px = win->user_buf_h;
    if (!src || sw_px <= 0 || sh_px <= 0 || cw <= 0 || ch <= 0) return;

    /* Clip the destination rect to the framebuffer */
    int sx = cx, sy = cy, sw = cw, sh = ch;
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > fw) sw = fw - sx;
    if (sy + sh > fh) sh = fh - sy;
    if (sw <= 0 || sh <= 0) return;

    /* ── Animated / scaled path ─────────────────────────────────────────
       During geometry tweens and open/close animations the window chrome is
       rendered at a different size than the buffer. Stretch the source to fit.
       Also used whenever the source and dest sizes differ (a pending tiled /
       maximized resize that the app has not flushed yet): the 1:1 path would
       wrap rows at the wrong stride and mirror the content, so any mismatch
       MUST take the ratio path — it never wraps. */
    if (win->geom_anim || win->anim_type != WM_ANIM_NONE ||
        cw != sw_px || ch != sh_px) {
        /* Guard against zero-size divisions during early animation frames */
        if (cw <= 0 || ch <= 0) return;
        for (int r = 0; r < sh; r++) {
            /* y in content-area space → source row */
            int cy_rel = (sy - cy) + r;   /* 0 at top of visible content */
            int by = cy_rel * sh_px / ch;
            if (by < 0) by = 0;
            if (by >= sh_px) by = sh_px - 1;
            uint32_t* dst = bb + (sy + r) * fw + sx;
            int cx_off = sx - cx;         /* x-offset of visible slice in content space */
            for (int cc = 0; cc < sw; cc++) {
                int bx = (cx_off + cc) * sw_px / cw;
                if (bx < 0) bx = 0;
                if (bx >= sw_px) bx = sw_px - 1;
                dst[cc] = src[by * sw_px + bx];
            }
        }
        return;
    }

    /* ── Steady-state 1:1 blit ──────────────────────────────────────────
       bw/bh == cw/ch so source and dest are always the same size.
       Any over-edge pixels (scrolled, or tiny resize rounding) are filled
       with the canvas background color. */
    int bsrc_x = (sx - cx) + win->view_x;
    int bsrc_y = (sy - cy) + win->view_y;
    for (int r = 0; r < sh; r++) {
        int by = bsrc_y + r;
        uint32_t* dst = bb + (sy + r) * fw + sx;
        for (int cc = 0; cc < sw; cc++) {
            int bx = bsrc_x + cc;
            if (by >= 0 && by < sh_px && bx >= 0 && bx < sw_px) {
                dst[cc] = src[by * sw_px + bx];
            } else {
                dst[cc] = 0x0D1117;
            }
        }
    }
}

static void render_window(wm_window_t* win) {
    /* Minimized windows are not drawn on the desktop */
    if (win->flags & WM_FLAG_MINIMIZED) return;

    int x = win->x, y = win->y, w = win->w, h = win->h;
    int is_focused = (win->flags & WM_FLAG_FOCUSED) != 0;
    int is_max = (win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V)) != 0;

    /* ── Geometry tween (maximize / restore / minimize) ──
     * Override the displayed geometry while a tween is active so the
     * window visibly grows/shrinks between its old and new rects. */
    if (win->geom_anim) {
        float t = gui_anim_progress(win->geom_anim_ms, GEOM_ANIM_MS);
        if (t >= 1.0f) {
            win->geom_anim = 0;
        } else {
            float e = gui_anim_ease(t, GUI_ANIM_EASEOUT);
            int fx = win->anim_from_x, fy = win->anim_from_y;
            int fw = win->anim_from_w, fh = win->anim_from_h;
            int tx = win->anim_to_x, ty = win->anim_to_y;
            int tw = win->anim_to_w, th = win->anim_to_h;
            x = fx + (int)((tx - fx) * e);
            y = fy + (int)((ty - fy) * e);
            w = fw + (int)((tw - fw) * e);
            h = fh + (int)((th - fh) * e);
            is_max = 0;   /* draw rounded during the tween; snaps at the end */
        }
    }

    /* ── Open / close animation transform ──
     * Translate/scale the window geometry for the first OPEN_ANIM_MS
     * after it opens (fade IN), and reverse it while WM_FLAG_CLOSING is
     * set (fade OUT). Maximized / half-maximized windows are not animated. */
    int anim = win->anim_type;
    int veil_alpha = 0;
    int closing = (win->flags & WM_FLAG_CLOSING) != 0;
    if (anim != WM_ANIM_NONE && !is_max) {
        float e;
        if (closing) {
            uint32_t now = timer_get_ms();
            int dt = (int)(now - win->close_ms);
            e = gui_anim_progress(win->close_ms, OPEN_ANIM_MS);   /* 0→1 */
        } else {
            uint32_t now = timer_get_ms();
            int dt = (int)(now - win->open_ms);
            if ((int)(now - win->open_ms) >= OPEN_ANIM_MS) e = 1.0f;
            else e = gui_anim_ease(gui_anim_progress(win->open_ms, OPEN_ANIM_MS), GUI_ANIM_EASEOUT);
        }
        if (e < 1.0f || closing) {
            float k = closing ? (1.0f - e) : e;     /* open: 0→1, close: 1→0 */
            if (anim == WM_ANIM_ZOOM) {
                int cx = win->x + win->w / 2, cy = win->y + win->h / 2;
                int sw = (int)(win->w * (0.7f + 0.3f * k));
                int sh = (int)(win->h * (0.7f + 0.3f * k));
                x = cx - sw / 2; y = cy - sh / 2; w = sw; h = sh;
            } else if (anim == WM_ANIM_SLIDE) {
                int off = (int)(win->w * (1.0f - k));
                x = closing ? (win->x - off) : (win->x - off);   /* slide out to left */
            } else if (anim == WM_ANIM_DROP) {
                int off = (int)(win->h * (1.0f - k));
                y = closing ? (win->y - off) : (win->y - off);   /* drop out to top */
            }
            /* fade: in on open, out on close */
            veil_alpha = closing ? (int)(e * 170.0f) : (int)((1.0f - e) * 150.0f);
        }
    }

    personalization_t* p = get_personalization();
    wm_theme_params_t tp; theme_get_wm_params(&tp);
    int r = is_max ? 0 : tp.corner_radius;
    /* App window chrome follows the active theme's window roles so the title
     * bar never looks like a washed-out grey strip and never renders white. */
    uint32_t bg_main    = theme_get_color(THEME_ROLE_WINDOW_BG);
    uint32_t border_clr = theme_get_color(THEME_ROLE_WINDOW_BORDER);
    uint32_t title_bg   = theme_get_color(THEME_ROLE_WINDOW_TITLE);
    uint32_t text_clr   = theme_get_color(THEME_ROLE_PRIMARY);
    int title_h = tp.titlebar_h;

    /* ── Window drop shadow (non-maximized, focused) ──
       Only recompute the (expensive) shadow while the window is actually
       moving (open / geometry / minimize tween). A settled window keeps a
       flat look — drawing an 8px blurred shadow over a large window every
       single frame is what made big apps like System / Theme Manager lag. */
    int animating = win->geom_anim || (win->flags & WM_FLAG_CLOSING) ||
                    (anim != WM_ANIM_NONE && (int)(timer_get_ms() - win->open_ms) < OPEN_ANIM_MS);
    if (is_focused && !is_max && animating) {
        gfx_draw_shadow(x, y, w, h, 8);
    }

    gfx_push_clip(x, y, w, h);

    if (is_max) {
        /* Maximized: no rounded corners, extend to screen edges */
        gfx_fill_rect(x, y + title_h, w, h - title_h, bg_main);
        gfx_fill_rect(x, y, w, title_h, title_bg);
        gfx_draw_hline(x, y + h - 1, w, border_clr);
        gfx_draw_vline(x, y, h, border_clr);
        gfx_draw_vline(x + w - 1, y, h, border_clr);
        gfx_draw_hline(x, y + h - 1, w, border_clr);
    } else if (r > 0) {
        gfx_fill_rect_rounded(x, y, w, h, r, bg_main);
        gfx_fill_rect_rounded(x, y, w, title_h, r, title_bg);
        gfx_draw_rect_rounded_outline(x, y, w, h, r, 1, border_clr);
    } else {
        gfx_fill_rect(x, y + title_h, w, h - title_h, bg_main);
        gfx_fill_rect(x, y, w, title_h, title_bg);
        gfx_draw_rect_outline(x, y, w, h, 1, border_clr);
    }

    gfx_draw_hline(x + 1, y + title_h, w - 2, gfx_darken(border_clr, 15));

    /* Title text is drawn later, after button positions are known (clip below) */

    /* ── Title-bar action buttons ──
     * Layout (from theme):
     *   0 = Default : minimize LEFT, maximize+close RIGHT
     *   1 = Haiku   : single close on LEFT, plain rectangle (turns red)
     *   2 = XP      : min+max+close RIGHT, classic                   */
    int btn_size = title_h - 12;   /* 20px square buttons */
    if (btn_size < 14) btn_size = 14;
    int btn_y = y + (title_h - btn_size) / 2;
    int gap = 4;
    int mx = mouse_get_x(), my = mouse_get_y();
    uint32_t chov = tp.close_hover ? tp.close_hover : 0xEF4444;

    int min_x, max_x, close_x;
    if (tp.close_only) {
        close_x = x + 6;                 /* Haiku: single close button on the LEFT */
        min_x = max_x = -9999;
    } else if (tp.winbtn_layout == 2) {  /* XP: all controls RIGHT */
        close_x = x + w - btn_size - 6;
        max_x   = close_x - btn_size - gap;
        min_x   = max_x - btn_size - gap;
    } else {                             /* Default: min LEFT, max+close RIGHT */
        min_x   = x + 6;
        close_x = x + w - btn_size - 6;
        max_x   = close_x - btn_size - gap;
    }

    /* Title: clipped so it never runs under buttons.
     *  close_only (Haiku) : starts after the left close btn, goes to right edge
     *  layout==2  (XP)    : starts at x+12, ends before the leftmost right-btn
     *  layout==0  (Default): starts after the left min btn, ends before max btn */
    int title_start, title_end;
    if (tp.close_only) {
        /* Haiku: close + min + max all on left side, 3 buttons with gap=3 */
        title_start = close_x + 3 * (btn_size + 3) + 6;
        title_end   = x + w - 8;
    } else if (tp.winbtn_layout == 2) {   /* XP: all buttons on right */
        title_start = x + 12;
        title_end   = min_x - 8;
    } else {                              /* Default: min left, max+close right */
        title_start = min_x + btn_size + 8;
        title_end   = max_x - 8;
    }
    int title_maxw = title_end - title_start;
    if (title_maxw < 8) title_maxw = 8;
    gfx_push_clip(title_start, y, title_maxw, title_h);
    gfx_draw_string_transparent(title_start, y + (title_h - 16) / 2, win->title, text_clr);
    gfx_pop_clip();

    /* Close button (hit area recorded for wm click handling) */
    win->close_btn_x = close_x - 4;
    win->close_btn_y = y + 2;
    win->close_btn_w = btn_size + 8;
    win->close_btn_h = title_h - 4;
    int close_hover = point_in_rect(mx, my, win->close_btn_x, win->close_btn_y, win->close_btn_w, win->close_btn_h);

    if (tp.close_only) {
        /* Haiku: yellow/gold button left-side, turns red on hover */
        uint32_t cb = close_hover ? 0xC0392B : 0xF6C500;
        uint32_t cb_top = close_hover ? 0xE8503A : 0xFDE040;
        uint32_t cb_bot = close_hover ? 0x8B2010 : 0xC89000;
        gfx_fill_rect(close_x, btn_y, btn_size, btn_size, cb);
        gfx_draw_rect_outline(close_x, btn_y, btn_size, btn_size, 1, close_hover ? 0x8B2010 : 0x8B7040);
        gfx_draw_hline(close_x + 1, btn_y + 1, btn_size - 2, cb_top);
        gfx_draw_hline(close_x + 1, btn_y + btn_size - 2, btn_size - 2, cb_bot);
        /* X mark */
        uint32_t cf = close_hover ? 0xFFFFFF : 0x3A2810;
        gfx_draw_string_transparent(close_x + (btn_size - 8) / 2, btn_y + (btn_size - 16) / 2 + 1, "x", cf);

        /* Haiku also has min/max to the right of close on left side */
        int next_x = close_x + btn_size + 3;
        int m2hov = point_in_rect(mx, my, next_x, btn_y, btn_size, btn_size);
        uint32_t m2b = m2hov ? 0xF6D840 : 0xD4A020;
        gfx_fill_rect(next_x, btn_y, btn_size, btn_size, m2b);
        gfx_draw_rect_outline(next_x, btn_y, btn_size, btn_size, 1, 0x8B7040);
        gfx_draw_hline(next_x + 1, btn_y + 1, btn_size - 2, gfx_lighten(m2b, 30));
        gfx_draw_hline(next_x + 5, btn_y + btn_size / 2 + 1, btn_size - 10, 0x3A2810);

        int next_x2 = next_x + btn_size + 3;
        int m3hov = point_in_rect(mx, my, next_x2, btn_y, btn_size, btn_size);
        uint32_t m3b = m3hov ? 0xF6D840 : 0xD4A020;
        gfx_fill_rect(next_x2, btn_y, btn_size, btn_size, m3b);
        gfx_draw_rect_outline(next_x2, btn_y, btn_size, btn_size, 1, 0x8B7040);
        gfx_draw_hline(next_x2 + 1, btn_y + 1, btn_size - 2, gfx_lighten(m3b, 30));
        gfx_draw_rect_outline(next_x2 + 4, btn_y + 4, btn_size - 8, btn_size - 8, 1, 0x3A2810);
    } else {
        uint32_t close_bg = close_hover ? chov : 0x1E1E24;
        uint32_t close_fg = close_hover ? 0xFFFFFF : 0xB7BECB;
        draw_tb_btn(close_x, btn_y, btn_size, close_bg, close_fg, &tp, 1);
        gfx_draw_string_transparent(close_x + (btn_size - 8) / 2, btn_y + (btn_size - 16) / 2 + 1, "x", close_fg);
    }

    /* XP "glassy" sheen: subtle light line just under the title top edge */
    if (tp.winbtn_layout == 2) {
        gfx_draw_hline(x + 3, y + 2, w - 6, 0x9CC3E8);
        gfx_draw_hline(x + 3, y + 3, w - 6, 0x6A9BD8);
    }

    int min_hover = 0, max_hover = 0;
    if (!is_max && !tp.close_only) {
        max_hover = point_in_rect(mx, my, max_x, btn_y, btn_size, btn_size);
        uint32_t max_bg = max_hover ? 0x3A3A40 : 0x1E1E24;
        uint32_t max_fg = max_hover ? 0xFFFFFF : 0xB7BECB;
        draw_tb_btn(max_x, btn_y, btn_size, max_bg, max_fg, &tp, 1);
        if (is_max) {
            gfx_draw_rect_outline(max_x + 4, btn_y + 4, btn_size - 8, btn_size - 8, 1, max_fg);
            gfx_draw_line(max_x + 4, btn_y + 4, max_x + btn_size - 7, btn_y + 4, max_fg);
            gfx_fill_rect(max_x + 6, btn_y + 6, btn_size - 12, 2, max_fg);
        } else {
            gfx_draw_rect_outline(max_x + 5, btn_y + 5, btn_size - 10, btn_size - 10, 1, max_fg);
        }

        min_hover = point_in_rect(mx, my, min_x, btn_y, btn_size, btn_size);
        uint32_t min_bg = min_hover ? 0x3A3A40 : 0x1E1E24;
        uint32_t min_fg = min_hover ? 0xFFFFFF : 0xB7BECB;
        draw_tb_btn(min_x, btn_y, btn_size, min_bg, min_fg, &tp, 1);
        gfx_draw_hline(min_x + 5, btn_y + btn_size / 2 + 1, btn_size - 10, min_fg);
    }
    (void)min_hover; (void)max_hover;

    gfx_push_clip(x + 1, y + title_h + 1, w - 2, h - title_h - 2);
    if (win->flags & WM_FLAG_USER) {
        /* Ring-3 app: composite its content buffer (honoring view scroll). */
        if (win->user_buf) wm_blit_user_buffer(win, x, y, w, h);
        gfx_pop_clip();   /* pop content clip (#3) */
        gfx_pop_clip();   /* pop window-rect clip (#1) — MUST balance the
                             push at the top of render_window, or the leaked
                             clip confines all later draws (taskbar/start/
                             search) to this window's rect. */
        return;
    }
    if (win->flags & WM_FLAG_HUNG) {
        /* Watchdog tripped: the window was rendering far too slowly for a
           sustained period. We still ATTEMPT its render this frame (so a
           merely-heavy-but-correct app keeps working and can recover), but
           show a notice. Genuinely stuck renderers get caught here too. */
        uint32_t t0 = timer_get_ms();
        if (win->on_render)
            win->on_render(win->id, x, y + title_h, w, h - title_h, win->view_x, win->view_y);
        uint32_t dt = timer_get_ms() - t0;
        if (dt <= 40) {                 /* recovered — render is fast again */
            win->flags &= ~WM_FLAG_HUNG;
            win->slow_frames = 0;
            win->hung_already = 0;
        } else {
            gfx_push_clip(x + 1, y + title_h + 1, w - 2, h - title_h - 2);
            gfx_draw_string_transparent(x + 16, y + title_h + 30,
                                        "App is very slow to render; still running.", 0xFFB4A8);
            gfx_draw_string_transparent(x + 16, y + title_h + 50,
                                        "Close the window if it stays stuck.", 0xC9A79E);
            gfx_pop_clip();
        }
    } else if (win->on_render) {
        uint32_t t0 = timer_get_ms();
        win->on_render(win->id, x, y + title_h, w, h - title_h, win->view_x, win->view_y);
        uint32_t dt = timer_get_ms() - t0;
        if (dt > 40) {
            /* One slow frame is normal (heavy scenes, first paint, GC). Only
               a SUSTAINED slowdown should trip the watchdog — a single heavy
               frame must never freeze a working app. Count it; pause only
               after ~40 consecutive slow frames (about a second). Apps that
               simply render rich content at 20-35 ms/frame will never trip. */
            int inc = (dt > 120) ? 2 : 1;
            win->slow_frames = (uint8_t)(win->slow_frames + inc);
            if (win->slow_frames >= 40) {
                win->flags |= WM_FLAG_HUNG;
                if (!win->hung_already) {
                    win->hung_already = 1;
                    kerror("App \"%s\" is very slow to render; still running.",
                           win->title ? win->title : "?");
                }
            }
        } else {
            win->slow_frames = 0;       /* fast frame — reset the counter */
        }
    }
    gfx_pop_clip();

    for (int b = 0; b < win->button_count; b++) {
        if (!win->buttons[b].is_active) continue;
        int bx = x + win->buttons[b].x;
        int by = y + WM_TITLEBAR_H + win->buttons[b].y;
        uint32_t btn_bg = win->buttons[b].is_hovered ? gfx_lighten(win->buttons[b].bg_color, 20) : win->buttons[b].bg_color;
        gfx_fill_rect_rounded(bx, by, win->buttons[b].w, win->buttons[b].h, 4, btn_bg);
        gfx_draw_string_transparent(bx + (win->buttons[b].w - gfx_strlen(win->buttons[b].label) * 8) / 2,
                                    by + (win->buttons[b].h - 16) / 2, win->buttons[b].label, win->buttons[b].fg_color);
    }

    if (win->content_h > (h - WM_TITLEBAR_H)) {
        int sb_w = 6, sb_x = x + w - sb_w - 2, sb_y = y + title_h + 2, sb_h = h - title_h - 4;
        gfx_fill_rect(sb_x, sb_y, sb_w, sb_h, 0x0D0E12);
        int thumb_h = (sb_h * sb_h) / win->content_h;
        if (thumb_h < 16) thumb_h = 16;
        int thumb_y = sb_y + (win->view_y * (sb_h - thumb_h)) / (win->content_h - (h - WM_TITLEBAR_H));
        gfx_fill_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, border_clr);
    }

        /* ── Resize handle dots (skip when maximized) ── */
    if (!is_max) {
        uint32_t handle_clr = 0x4D5059;
        /* Draw at bottom-right corner, just inside the border */
        for (int i = 0; i < 3; i++) {
            uint32_t c = gfx_lerp_color(handle_clr, bg_main, i, 3);
            gfx_fill_rect(x + w - 8 - i*4, y + h - 8 + i*4, 3, 3, c);
        }
    }

    gfx_pop_clip();
}

static void draw_resize_grid(int x, int y, int w, int h, uint32_t color) {
    int cl = 16;
    gfx_draw_line(x, y, x + cl, y, color);
    gfx_draw_line(x, y, x, y + cl, color);
    gfx_draw_line(x + w, y, x + w - cl, y, color);
    gfx_draw_line(x + w, y, x + w, y + cl, color);
    gfx_draw_line(x, y + h, x + cl, y + h, color);
    gfx_draw_line(x, y + h, x, y + h - cl, color);
    gfx_draw_line(x + w, y + h, x + w - cl, y + h, color);
    gfx_draw_line(x + w, y + h, x + w, y + h - cl, color);
    gfx_draw_rect_outline(x, y, w, h, 1, color);
}

int wm_tick(void) {
    int mx = mouse_get_x(), my = mouse_get_y(), mbtn = mouse_get_buttons();

    /* ── Close-animation cleanup: free windows whose close tween finished ── */
    {
        uint32_t now = timer_get_ms();
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            wm_window_t* w = &windows[i];
            if ((w->flags & WM_FLAG_CLOSING) &&
                (int)(now - w->close_ms) >= OPEN_ANIM_MS) {
                int cid = w->id;
                wm_user_free_buffer(w);
                w->id = -1; w->flags = 0;
                if (window_count > 0) window_count--;
                if (focused_id == cid) focused_id = -1;
            }
        }
    }

    /* ── Minimize-animation cleanup: once the shrink tween ends, hide the
       window (set MINIMIZED). Its logical geometry was left untouched, so
       un-minimizing restores it correctly. ── */
    {
        uint32_t now = timer_get_ms();
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            wm_window_t* w = &windows[i];
            if (w->geom_anim == 2 && (int)(now - w->geom_anim_ms) >= GEOM_ANIM_MS) {
                w->geom_anim = 0;
                w->flags |= WM_FLAG_MINIMIZED;
            }
        }
    }

    /* Consume the driver's press-edge latch when present: it carries the
       position where the press STARTED and survives taps shorter than one
       frame, which the edge test below would miss. We only use the latch
       FLAG (so a fast tap is never dropped); the click POSITION is taken
       from the live cursor (mouse_get_x/y) so a click always lands exactly
       under the pointer the user sees — using the latched coordinate would
       register the press at a spot the cursor had already moved away from,
       which made clicks hit the wrong control. */
    int lcx, lcy;
    int latched = mouse_consume_click(&lcx, &lcy);
    (void)lcx; (void)lcy;
    int clicked = latched || ((mbtn & 1) && !(prev_mouse_btn & 1));

    /* Fill the unified per-frame input snapshot */
    snap_mx = mx; snap_my = my; snap_mbtn = mbtn;
    snap_lc = latched || ((mbtn & 1) && !(prev_mouse_btn & 1));
    snap_lr = !(mbtn & 1) && (prev_mouse_btn & 1);
    snap_rc = (mbtn & 2) && !(prev_mouse_btn & 2);
    snap_rr = !(mbtn & 2) && (prev_mouse_btn & 2);
    snap_ac = latched || ((mbtn != 0) && (prev_mouse_btn == 0));
    snap_ar = (mbtn == 0) && (prev_mouse_btn != 0);

    /* Ring-3 focused window: forward mouse state as queue events.
       Move is throttled to coordinate changes; press/release edges are
       only forwarded when the cursor is over the window content. */
    {
        wm_window_t* ufw = (focused_id >= 0) ? find_window(focused_id) : 0;
        if (ufw && (ufw->flags & WM_FLAG_USER)) {
            int relx = mx - ufw->x;
            int rely = my - (ufw->y + WM_TITLEBAR_H);
            if (relx >= 0 && rely >= 0 && relx < ufw->user_buf_w && rely < ufw->user_buf_h) {
                if (relx != ufw->user_lmx || rely != ufw->user_lmy) {
                    wm_user_enqueue(ufw->id, WM_USER_EV_MMOVE, relx, rely, 0, 0);
                    ufw->user_lmx = relx;
                    ufw->user_lmy = rely;
                }
                if (snap_lc) wm_user_enqueue(ufw->id, WM_USER_EV_MDOWN, relx, rely, 1, 0);
                if (snap_lr) wm_user_enqueue(ufw->id, WM_USER_EV_MUP, relx, rely, 1, 0);
                if (snap_rc) wm_user_enqueue(ufw->id, WM_USER_EV_MDOWN, relx, rely, 2, 0);
                if (snap_rr) wm_user_enqueue(ufw->id, WM_USER_EV_MUP, relx, rely, 2, 0);
            }
        }
    }

    int key = get_key_ex();
    if (key & 0x100) {
        /* Key release: only ring-3 user windows consume releases (DOOM
           needs keyups to clear its gamekeydown state). Skip all
           shortcuts, menus and desktop handling for releases. */
        if (focused_id >= 0) {
            wm_window_t* uw = find_window(focused_id);
            if (uw && (uw->flags & WM_FLAG_USER))
                wm_user_enqueue(uw->id, WM_USER_EV_KEY, key, 0, 0, 0);
        }
        key = 0;
    }
    /* Ctrl edge tracking: DOOM binds plain ctrl as fire, but the kernel
       only reports ctrl+letter combos (1-26). Synthesize ctrl press/release
       events (scancode 0x1D) for the focused ring-3 user window. */
    {
        static int wm_ctrl_down = 0;
        int ctrl_now = keyboard_is_key_down(0x1D);
        if (ctrl_now != wm_ctrl_down) {
            wm_ctrl_down = ctrl_now;
            if (focused_id >= 0) {
                wm_window_t* uw = find_window(focused_id);
                if (uw && (uw->flags & WM_FLAG_USER))
                    wm_user_enqueue(uw->id, WM_USER_EV_KEY,
                                    ctrl_now ? 0x1D : 0x11D, 0, 0, 0);
            }
        }
    }
    /* Alt edge tracking (scancode 0x38): nuklear apps use Alt to open the
       app menu bar, mirroring the ctrl synthesis above. */
    {
        static int wm_alt_down = 0;
        int alt_now = keyboard_is_key_down(0x38);
        if (alt_now != wm_alt_down) {
            wm_alt_down = alt_now;
            if (focused_id >= 0) {
                wm_window_t* uw = find_window(focused_id);
                if (uw && (uw->flags & WM_FLAG_USER))
                    wm_user_enqueue(uw->id, WM_USER_EV_KEY,
                                    alt_now ? 0x38 : 0x138, 0, 0, 0);
            }
        }
    }

    uint32_t fw = get_fb_width(), fh = get_fb_height();

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* win = &windows[i];
        if (!(win->flags & WM_FLAG_VISIBLE)) continue;
        if (win->flags & WM_FLAG_DRAGGING) {
            if (g_tile_enabled) { win->flags &= ~WM_FLAG_DRAGGING; }
            else if (mbtn & 1) {
                /* If maximized, unmaximize first and adjust offset */
                if (win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V)) {
                    win->flags &= ~(WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V);
                    win->x = mx - win->drag_offset_x;
                    win->y = my - win->drag_offset_y;
                    win->w = win->saved_w; win->h = win->saved_h;
                    win->drag_offset_x = mx - win->x;
                    win->drag_offset_y = my - win->y;
                } else {
                    win->x = mx - win->drag_offset_x;
                    win->y = my - win->drag_offset_y;
                }
                /* Clamp to screen bounds */
                if (win->x < 0) win->x = 0;
                if (win->y < 0) win->y = 0;
                int max_x = (int)fw - win->w;
                if (win->x > max_x) win->x = max_x > 0 ? max_x : 0;
                int max_y = (int)fh - TASKBAR_H - win->h;
                if (win->y > max_y) win->y = max_y > 0 ? max_y : 0;
            } else win->flags &= ~WM_FLAG_DRAGGING;
        }
        if (win->flags & WM_FLAG_RESIZING) {
            if (g_tile_enabled) { win->flags &= ~WM_FLAG_RESIZING; }
            else if (mbtn & 1) {
                /* If maximized, unmaximize first */
                if (win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V)) {
                    win->flags &= ~(WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V);
                    win->x = win->saved_x; win->y = win->saved_y;
                    win->w = win->saved_w; win->h = win->saved_h;
                    win->drag_offset_x = mx - win->x;
                    win->drag_offset_y = my - win->y;
                }
                int dw = mx - win->drag_offset_x;
                int dh = my - win->drag_offset_y;
                win->w = win->start_w + dw;
                win->h = win->start_h + dh;
                if (win->w < WM_MIN_W) win->w = WM_MIN_W;
                if (win->h < WM_MIN_H) win->h = WM_MIN_H;
                /* Clamp to screen */
                if (win->w > (int)fw) win->w = (int)fw;
                if (win->h > (int)fh - TASKBAR_H) win->h = (int)fh - TASKBAR_H;
                win->content_w = win->w;
                win->content_h = win->h - WM_TITLEBAR_H;
                if (win->flags & WM_FLAG_USER)
                    wm_user_mark_resize(win, win->w - 2, win->h - WM_TITLEBAR_H - 2);
                if (win->on_resize) win->on_resize(win->id, win->w, win->h);
            } else win->flags &= ~WM_FLAG_RESIZING;
        }
    }

        if (clicked) {
        wm_window_t* hit = window_at_point(mx, my);
        if (hit) {
            /* Skip all title-bar actions for minimized windows — they should
               be restored via the taskbar, not by clicking invisible content. */
            if (hit->flags & WM_FLAG_MINIMIZED) {
                bring_to_front(hit);
                goto done;
            }

            bring_to_front(hit);

            /* fullscreen/maximized flag, for button layout decisions */
            int is_max = (hit->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V)) != 0;

            /* ── Compute title-bar button hit areas (match render order) ── */
            int btn_size = WM_TITLEBAR_H - 12;   /* 20px */
            int close_x = hit->x + hit->w - btn_size - 6;
            int btn_y0 = hit->y + 2;

            /* 1. Close button (always present, rightmost) */
            if (point_in_rect(mx, my, close_x, btn_y0, btn_size, btn_size)) {
                if (hit->flags & WM_FLAG_USER)
                    wm_user_enqueue(hit->id, WM_USER_EV_CLOSE, 0, 0, 0, 0);
                else
                    wm_close_window(hit->id);
                goto done;
            }

            if (is_max) {
                /* Fullscreen: close is the only control. */
            } else {
                /* 2. Maximize / Restore (right, next to close) */
                int max_x = close_x - btn_size - 4;
                if (point_in_rect(mx, my, max_x, btn_y0, btn_size, btn_size)) {
                    if (hit->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V))
                        wm_restore_window(hit->id);
                    else
                        wm_maximize_window(hit->id, WM_MAX_FULL);
                    goto done;
                }

                /* 3. Minimize button (LEFT side) */
                int min_x = hit->x + 6;
                if (point_in_rect(mx, my, min_x, btn_y0, btn_size, btn_size)) {
                    wm_minimize_window(hit->id);
                    goto done;
                }
            }

            /* 4. Resize handle (bottom-right corner) */
            if (point_in_rect(mx, my, hit->x + hit->w - 20, hit->y + hit->h - 20, 20, 20)) {
                hit->flags |= WM_FLAG_RESIZING;
                hit->start_w = hit->w; hit->start_h = hit->h;
                hit->drag_offset_x = mx; hit->drag_offset_y = my;
                goto done;
            }

            /* 5. Title bar: double-click to maximize, single-click to drag */
            if (point_in_rect(mx, my, hit->x, hit->y, hit->w, WM_TITLEBAR_H)) {
                uint32_t now = timer_get_ms();
                if (now - hit->last_title_click_ms < 500 &&
                    ((mx - hit->title_click_x) < 0 ? -(mx - hit->title_click_x) : (mx - hit->title_click_x)) < 5 &&
                    ((my - hit->title_click_y) < 0 ? -(my - hit->title_click_y) : (my - hit->title_click_y)) < 5) {
                    /* Double-click: toggle maximize */
                    if (hit->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V))
                        wm_restore_window(hit->id);
                    else
                        wm_maximize_window(hit->id, WM_MAX_FULL);
                    goto done;
                }
                hit->last_title_click_ms = now;
                hit->title_click_x = mx; hit->title_click_y = my;
                hit->flags |= WM_FLAG_DRAGGING;
                hit->drag_offset_x = mx - hit->x; hit->drag_offset_y = my - hit->y;
            }

            /* 6. Scrollbar hit area */
            if (hit->content_h > (hit->h - WM_TITLEBAR_H)) {
                int sb_w = 10, sb_x = hit->x + hit->w - sb_w - 2;
                if (point_in_rect(mx, my, sb_x, hit->y + WM_TITLEBAR_H, sb_w, hit->h - WM_TITLEBAR_H)) {
                    hit->view_y = ((my - (hit->y + WM_TITLEBAR_H)) * hit->content_h) / (hit->h - WM_TITLEBAR_H) - (hit->h / 4);
                    if (hit->view_y < 0) hit->view_y = 0;
                    int max_v = hit->content_h - (hit->h - WM_TITLEBAR_H);
                    if (hit->view_y > max_v) hit->view_y = (max_v > 0 ? max_v : 0);
                    goto done;
                }
            }

            /* 7. Embedded window buttons */
            for (int b = 0; b < hit->button_count; b++) {
                if (!hit->buttons[b].is_active) continue;
                int bx = hit->x + hit->buttons[b].x;
                int by = hit->y + WM_TITLEBAR_H + hit->buttons[b].y;
                if (point_in_rect(mx, my, bx, by, hit->buttons[b].w, hit->buttons[b].h)) {
                    if (hit->buttons[b].on_click) hit->buttons[b].on_click(hit->id, hit->buttons[b].id);
                    goto done;
                }
            }

        } else if (my < (int)fh - 36) {
            focused_id = -1; for (int i = 0; i < WM_MAX_WINDOWS; i++) windows[i].flags &= ~WM_FLAG_FOCUSED;
        }
    } /* end if (clicked) */

done:
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* win = &windows[i];
        if (!(win->flags & WM_FLAG_VISIBLE)) continue;
        for (int b = 0; b < win->button_count; b++) {
            win->buttons[b].is_hovered = win->buttons[b].is_active && point_in_rect(mx, my, win->x + win->buttons[b].x, win->y + WM_TITLEBAR_H + win->buttons[b].y, win->buttons[b].w, win->buttons[b].h);
        }
    }

    extern void gui_toggle_start_menu(void), gui_toggle_search(void), gui_open_search(void), gui_handle_menu_key(char), gui_handle_search_key(char), gui_handle_topbar_cfg_key(char);
    extern int gui_is_menu_open(void), gui_is_search_open(void), gui_is_topbar_cfg_open(void), gui_is_overlay_open(void);
    if ((keyboard_is_key_down(0x5B) || keyboard_is_key_down(0x5C)) && (key == 's' || key == 'S')) { gui_toggle_search(); key = 0; }
    else if ((keyboard_is_key_down(0x5B) || keyboard_is_key_down(0x5C)) && (key == 't' || key == 'T')) { wm_set_tiling(!wm_get_tiling()); if (wm_get_tiling()) wm_tile_all(); key = 0; }
    else if (key == 132) { gui_toggle_start_menu(); key = 0; }
    /* Custom user shortcuts (Super/Ctrl/Alt/Shift + key combo) */
    if (key != 0) {
        int mods = 0;
        if (keyboard_is_key_down(0x5B) || keyboard_is_key_down(0x5C)) mods |= 1;
        if (keyboard_is_key_down(0x1D) || keyboard_is_key_down(0x9D)) mods |= 2;
        if (keyboard_is_key_down(0x38)) mods |= 4;
        if (keyboard_is_key_down(0x2A) || keyboard_is_key_down(0x36)) mods |= 8;
        int k = key; if (k>='A'&&k<='Z') k += 32;
        if (k>='a'&&k<='z') { custom_shortcut_on_key(k, mods); }
    }
    if (key != 0) {
        if (gui_is_topbar_cfg_open()) { gui_handle_topbar_cfg_key(key); key = 0; }
        else if (gui_is_search_open()) { gui_handle_search_key(key); key = 0; }
        else if (gui_is_menu_open()) { gui_handle_menu_key(key); key = 0; }

        if (focused_id >= 0) {
            wm_window_t* fw = find_window(focused_id);
            int has_scroll = fw && (fw->content_h > (fw->h - WM_TITLEBAR_H));
            if (has_scroll && (keyboard_is_key_down(0x2A) || keyboard_is_key_down(0x36))) {
                if (KEY_MATCH(key, KEY_UP)) { fw->view_y -= 40; key = 0; }
                else if (KEY_MATCH(key, KEY_DOWN)) { fw->view_y += 40; key = 0; }
                if (fw->view_y < 0) fw->view_y = 0;
                int max_v = fw->content_h - (fw->h - WM_TITLEBAR_H);
                if (fw->view_y > max_v) fw->view_y = (max_v > 0 ? max_v : 0);
            }
        }
    }

    if (key && (keyboard_is_key_down(0x5B) || keyboard_is_key_down(0x5C))) {
        if (key == KEY_LEFT && current_desktop > 0) { wm_set_current_desktop(current_desktop - 1); key = 0; }
        else if (key == KEY_RIGHT && current_desktop < DESKTOP_COUNT - 1) { wm_set_current_desktop(current_desktop + 1); key = 0; }
    }

    if (key && focused_id >= 0) { wm_window_t* fw = find_window(focused_id);
        if (fw && (fw->flags & WM_FLAG_USER)) wm_user_enqueue(fw->id, WM_USER_EV_KEY, (int)(unsigned char)key, 0, 0, 0);
        else if (fw && fw->on_key) fw->on_key(fw->id, key); }

    wm_window_t* sorted[WM_MAX_WINDOWS]; int count; get_sorted_windows(sorted, &count);
    for (int i = 0; i < count; i++) {
        render_window(sorted[i]);
        if (sorted[i]->flags & WM_FLAG_RESIZING) {
            draw_resize_grid(sorted[i]->x, sorted[i]->y, sorted[i]->w, sorted[i]->h,
                           (get_personalization()->theme == 0) ? 0x6B7280 : 0x9CA3AF);
        }
    }

    /* Mouse wheel scroll — run AFTER the render pass so app render callbacks
       (e.g. Mandelbrot / Julia zoom) get first crack at the raw
       delta. Whatever remains is forwarded to the focused window as scroll
       key events and used for WM content viewport scrolling. */
    if (!gui_is_overlay_open()) {
        int wdelta = mouse_get_wheel_delta();
        if (wdelta != 0) {
            mouse_clear_wheel_delta();
            wm_window_t* fw = find_window(focused_id);
            int wkey = wdelta > 0 ? 128 : 129;   /* KEY_UP / KEY_DOWN */
            if (fw && (fw->flags & WM_FLAG_USER)) {
                int steps = wdelta > 0 ? wdelta : -wdelta;
                if (steps > 8) steps = 8;
                for (int s = 0; s < steps; s++)
                    wm_user_enqueue(fw->id, WM_USER_EV_KEY, wkey, 0, 0, 0);
            } else if (fw && fw->on_key) {
                int steps = wdelta > 0 ? wdelta : -wdelta;
                if (steps > 8) steps = 8;
                for (int s = 0; s < steps; s++) {
                    /* 128=KEY_UP (scroll up), 129=KEY_DOWN (scroll down) */
                    fw->on_key(fw->id, wdelta > 0 ? (char)128 : (char)129);
                }
            }
            /* Also scroll wm content viewport if window has content_h scrolling */
            if (fw && fw->content_h > (fw->h - WM_TITLEBAR_H)) {
                fw->view_y -= wdelta * 20;
                if (fw->view_y < 0) fw->view_y = 0;
                int max_v = fw->content_h - (fw->h - WM_TITLEBAR_H);
                if (fw->view_y > max_v) fw->view_y = (max_v > 0 ? max_v : 0);
            }
        }
     }

    prev_mouse_btn = mbtn;
    prev_mx = mx; prev_my = my;
    return 0;
}

void wm_run_single(int win_id) {
    wm_window_t* win = find_window(win_id);
    if (!win) return;
}

wm_window_t* wm_get_window_by_index(int index) {
    if (index < 0 || index >= WM_MAX_WINDOWS || !(windows[index].flags & WM_FLAG_VISIBLE)) return 0;
    return &windows[index];
}

void wm_bring_to_front(int win_id) { wm_window_t* win = find_window(win_id); if (win) bring_to_front(win); }
int wm_get_focused(void) { return focused_id; }
wm_window_t* wm_get_window(int id) { return find_window(id); }
int wm_get_window_count(void) { return window_count; }
int wm_window_at_point(int px, int py) {
    wm_window_t* hit = window_at_point(px, py);
    return hit ? hit->id : -1;
}
void wm_set_app_data(int win_id, void* data) { wm_window_t* win = find_window(win_id); if (win) win->app_data = data; }
void* wm_get_app_data(int win_id) { wm_window_t* win = find_window(win_id); return win ? win->app_data : 0; }

int wm_get_current_desktop(void) { return current_desktop; }

void wm_set_current_desktop(int d) {
    if (d < 0 || d >= DESKTOP_COUNT) return;
    if (d != current_desktop) desktop_prev[current_desktop] = d;
    current_desktop = d;
}

int wm_get_previous_desktop(void) {
    return desktop_prev[current_desktop];
}

void wm_refresh_all_button_theme(void) {
    uint32_t btn_bg = theme_get_color(THEME_ROLE_BUTTON_BG);
    uint32_t btn_text = theme_get_color(THEME_ROLE_BUTTON_TEXT);
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* win = &windows[i];
        if (win->id == -1) continue;
        for (int b = 0; b < win->button_count; b++) {
            if (!win->buttons[b].is_active) continue;
            if (win->buttons[b].bg_color == 0x6B7280 || win->buttons[b].bg_color == 0xEF4444 ||
                win->buttons[b].bg_color == 0x30363D || win->buttons[b].bg_color == 0x3A3A3C) {
                win->buttons[b].bg_color = btn_bg;
            }
            if (win->buttons[b].fg_color == 0x000000 || win->buttons[b].fg_color == 0xFFFFFF) {
                win->buttons[b].fg_color = btn_text;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 * Window state queries
 * ═══════════════════════════════════════════════════════════ */
int wm_get_window_title(int id, char* buf, int bufsize) {
    wm_window_t* win = find_window(id);
    if (!win || !buf || bufsize <= 0) return -1;
    int k = 0;
    while (win->title[k] && k < bufsize - 1) { buf[k] = win->title[k]; k++; }
    buf[k] = 0;
    return k;
}

uint32_t wm_get_window_accent(int id) {
    wm_window_t* win = find_window(id);
    return win ? win->accent_color : 0;
}

int wm_window_is_minimized(int id) {
    wm_window_t* win = find_window(id);
    return win ? ((win->flags & WM_FLAG_MINIMIZED) != 0) : 0;
}

int wm_window_is_maximized(int id) {
    wm_window_t* win = find_window(id);
    return win ? ((win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V)) != 0) : 0;
}

int wm_window_is_visible(int id) {
    wm_window_t* win = find_window(id);
    return win ? ((win->flags & WM_FLAG_VISIBLE) != 0) : 0;
}

void wm_set_window_title(int win_id, const char* title) {
    wm_window_t* win = find_window(win_id);
    if (!win || !title) return;
    int k = 0;
    while (title[k] && k < 63) { win->title[k] = title[k]; k++; }
    win->title[k] = 0;
}

void wm_minimize_window(int win_id) {
    wm_window_t* win = find_window(win_id);
    if (!win) return;
    if (win->flags & WM_FLAG_MINIMIZED) return;

    /* Remember current geometry so we can restore position on un-minimize */
    win->saved_x = win->x; win->saved_y = win->y;
    win->saved_w = win->w; win->saved_h = win->h;

    /* Begin a shrink-toward-taskbar animation. The window stays logically
       where it is (so un-minimize restores correctly); WM_FLAG_MINIMIZED is
       set when the tween finishes (see wm_tick). */
    uint32_t fh = get_fb_height();
    int tw = 64, th = 44;
    int tx = win->x, ty = (int)fh - TASKBAR_H - th;
    win->anim_from_x = win->x; win->anim_from_y = win->y;
    win->anim_from_w = win->w; win->anim_from_h = win->h;
    win->anim_to_x = tx; win->anim_to_y = ty;
    win->anim_to_w = tw; win->anim_to_h = th;
    win->geom_anim_ms = timer_get_ms();
    win->geom_anim = 2;

    win->flags &= ~(WM_FLAG_FOCUSED);

    /* Give focus to the next highest z-order visible window */
    int best_z = -1, best_id = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* w = &windows[i];
        if (w->id == -1 || w == win) continue;
        if (!(w->flags & WM_FLAG_VISIBLE)) continue;
        if (w->flags & WM_FLAG_MINIMIZED) continue;
        if (w->z_order > best_z) { best_z = w->z_order; best_id = w->id; }
    }
    focused_id = best_id;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].flags &= ~WM_FLAG_FOCUSED;
        if (windows[i].id == best_id) windows[i].flags |= WM_FLAG_FOCUSED;
    }
}
void wm_maximize_window(int win_id, int mode) {
    wm_window_t* win = find_window(win_id);
    if (!win) return;
    /* Ctrl edge tracking: DOOM binds plain ctrl as fire, but the kernel
       only reports ctrl+letter combos (1-26). Synthesize ctrl press/release
       events (scancode 0x1D) for the focused ring-3 user window. */
    {
        static int wm_ctrl_down = 0;
        int ctrl_now = keyboard_is_key_down(0x1D);
        if (ctrl_now != wm_ctrl_down) {
            wm_ctrl_down = ctrl_now;
            if (focused_id >= 0) {
                wm_window_t* uw = find_window(focused_id);
                if (uw && (uw->flags & WM_FLAG_USER))
                    wm_user_enqueue(uw->id, WM_USER_EV_KEY,
                                    ctrl_now ? 0x1D : 0x11D, 0, 0, 0);
            }
        }
    }
    /* Alt edge tracking (scancode 0x38): nuklear apps use Alt to open the
       app menu bar, mirroring the ctrl synthesis above. */
    {
        static int wm_alt_down = 0;
        int alt_now = keyboard_is_key_down(0x38);
        if (alt_now != wm_alt_down) {
            wm_alt_down = alt_now;
            if (focused_id >= 0) {
                wm_window_t* uw = find_window(focused_id);
                if (uw && (uw->flags & WM_FLAG_USER))
                    wm_user_enqueue(uw->id, WM_USER_EV_KEY,
                                    alt_now ? 0x38 : 0x138, 0, 0, 0);
            }
        }
    }

    uint32_t fw = get_fb_width(), fh = get_fb_height();
    if (fw == 0 || fh == 0) return;

    /* If currently maximized, restore first */
    if (win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V))
        wm_restore_window(win_id);

    /* Save the pre-maximized geometry */
    win->saved_x = win->x; win->saved_y = win->y;
    win->saved_w = win->w; win->saved_h = win->h;

    win->flags &= ~(WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V);

    int wx = 0, wy = 0, ww = (int)fw, wh = (int)fh;
    gui_get_work_area(&wx, &wy, &ww, &wh);

    switch (mode) {
        case WM_MAX_HORIZ:
            win->x = wx; win->y = wy;
            win->w = ww / 2; win->h = wh;
            win->flags |= WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H;
            break;
        case WM_MAX_RIGHT:
            win->x = wx + ww / 2; win->y = wy;
            win->w = ww - (ww / 2); win->h = wh;
            win->flags |= WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H;
            break;
        case WM_MAX_VERT:
            win->x = wx; win->y = wy;
            win->w = ww; win->h = wh / 2;
            win->flags |= WM_FLAG_MAXIMIZED | WM_FLAG_MAX_V;
            break;
        case WM_MAX_BOTTOM:
            win->x = wx; win->y = wy + (wh / 2);
            win->w = ww; win->h = wh - (wh / 2);
            win->flags |= WM_FLAG_MAXIMIZED | WM_FLAG_MAX_V;
            break;
        case WM_MAX_FULL:
        default:
            win->x = wx; win->y = wy;
            win->w = ww; win->h = wh;
            win->flags |= WM_FLAG_MAXIMIZED;
            break;
    }

    if (win->w < WM_MIN_W) win->w = WM_MIN_W;
    if (win->h < WM_MIN_H) win->h = WM_MIN_H;

    if (win->on_resize) win->on_resize(win->id, win->w, win->h);
    win->content_w = win->w;
    win->content_h = win->h - WM_TITLEBAR_H;
    if (win->flags & WM_FLAG_USER)
        wm_user_mark_resize(win, win->w - 2, win->h - WM_TITLEBAR_H - 2);
    win->flags |= WM_FLAG_VISIBLE;
    win->flags &= ~WM_FLAG_MINIMIZED;

    /* animate from the previous (restored) rect to the new maximized rect */
    start_geom_tween(win, win->x, win->y, win->w, win->h, 1);
}

void wm_restore_window(int win_id) {
    wm_window_t* win = find_window(win_id);
    if (!win) return;

    win->flags &= ~(WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V | WM_FLAG_MINIMIZED);

    win->x = win->saved_x; if (win->x < 0) win->x = 0;
    win->y = win->saved_y; if (win->y < 0) win->y = 0;
    win->w = win->saved_w; if (win->w < WM_MIN_W) win->w = WM_MIN_W;
    win->h = win->saved_h; if (win->h < WM_MIN_H) win->h = WM_MIN_H;

    /* Ctrl edge tracking: DOOM binds plain ctrl as fire, but the kernel
       only reports ctrl+letter combos (1-26). Synthesize ctrl press/release
       events (scancode 0x1D) for the focused ring-3 user window. */
    {
        static int wm_ctrl_down = 0;
        int ctrl_now = keyboard_is_key_down(0x1D);
        if (ctrl_now != wm_ctrl_down) {
            wm_ctrl_down = ctrl_now;
            if (focused_id >= 0) {
                wm_window_t* uw = find_window(focused_id);
                if (uw && (uw->flags & WM_FLAG_USER))
                    wm_user_enqueue(uw->id, WM_USER_EV_KEY,
                                    ctrl_now ? 0x1D : 0x11D, 0, 0, 0);
            }
        }
    }
    /* Alt edge tracking (scancode 0x38): nuklear apps use Alt to open the
       app menu bar, mirroring the ctrl synthesis above. */
    {
        static int wm_alt_down = 0;
        int alt_now = keyboard_is_key_down(0x38);
        if (alt_now != wm_alt_down) {
            wm_alt_down = alt_now;
            if (focused_id >= 0) {
                wm_window_t* uw = find_window(focused_id);
                if (uw && (uw->flags & WM_FLAG_USER))
                    wm_user_enqueue(uw->id, WM_USER_EV_KEY,
                                    alt_now ? 0x38 : 0x138, 0, 0, 0);
            }
        }
    }

    uint32_t fw = get_fb_width(), fh = get_fb_height();
    if (win->x + win->w > (int)fw) win->x = (int)fw - win->w;
    if (win->y + win->h > (int)fh) win->y = (int)fh - win->h;
    if (win->x < 0) win->x = 0;

    if (win->on_resize) win->on_resize(win->id, win->w, win->h);
    win->content_w = win->w;
    win->content_h = win->h - WM_TITLEBAR_H;
    if (win->flags & WM_FLAG_USER)
        wm_user_mark_resize(win, win->w - 2, win->h - WM_TITLEBAR_H - 2);

    bring_to_front(win);
    start_geom_tween(win, win->x, win->y, win->w, win->h, 1);  /* animate back to restored rect */
}

void wm_toggle_maximize(int win_id) {
    wm_window_t* win = find_window(win_id);
    if (!win) return;
    if (win->flags & (WM_FLAG_MAXIMIZED | WM_FLAG_MAX_H | WM_FLAG_MAX_V))
        wm_restore_window(win_id);
    else
        wm_maximize_window(win_id, WM_MAX_FULL);
}

/* Window snapping (drag-to-edge helper usable by apps / WM) */
void wm_snap_window(int win_id, int mode) {
    wm_maximize_window(win_id, mode);
}

void wm_set_window_accent(int win_id, uint32_t accent) {
    wm_window_t* win = find_window(win_id);
    if (win) win->accent_color = accent;
}

