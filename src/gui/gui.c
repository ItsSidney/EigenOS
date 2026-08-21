/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "gui/gui.h"
#include "gui/screensaver.h"
#include "handler/error.h"
#include "gui/gui_anim.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard.h"
#include "drivers/time/rtc.h"
#include "kernel/time/timer.h"
#include "gui/ui.h"
#include "gui/wm.h"
#include "gui/icons.h"
#include "gui/app_icons.h"
#include "kernel/security/security.h"
#include "kernel/mem/kheap.h"
#include "kernel/acpi.h"
#include "gui/wallpaper_mgr.h"
#include "libs/bmp.h"
#include "drivers/audio/audio.h"
#include "filesystem/filesystem.h"
#include "kernel/lib/string.h"
#include "kernel/task/task.h"
#include <gfx/splash_bmp.h>
#include <stdio.h>

extern volatile uint64_t timer_ticks;

#define WALLPAPER_CACHE_W 1920
#define WALLPAPER_CACHE_H 1080
static uint32_t wallpaper_cache[WALLPAPER_CACHE_W * WALLPAPER_CACHE_H];

/* Wallpaper-apply circle reveal animation state */
static uint32_t* g_wp_old = NULL;        /* snapshot of the previous wallpaper */

/* Debug: heap address of the wallpaper snapshot buffer for crash dumps. */
uint64_t gui_wp_old_addr(void) { return (uint64_t)(uintptr_t)g_wp_old; }
uint64_t gui_wp_old_size(void) { return (uint64_t)WALLPAPER_CACHE_W * WALLPAPER_CACHE_H * 4; }
static uint32_t  g_wp_anim_ms = 0;       /* start time of the reveal (0 = idle) */
static uint32_t  g_wp_anim_accent = 0;   /* accent ring colour */
#define WP_ANIM_MS 650

/* integer square root (no libc dependency) */
static uint32_t isqrt_u32(uint32_t n) {
    uint32_t r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* Capture the currently-displayed wallpaper so the apply animation can
 * reveal the new one inside an expanding circle over the old one. */
void gui_wallpaper_apply_anim(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    if (!g_wp_old)
        g_wp_old = (uint32_t*)kmalloc(WALLPAPER_CACHE_W * WALLPAPER_CACHE_H * 4);
    if (!g_wp_old) return;
    uint32_t n = fw * fh;
    if (n > WALLPAPER_CACHE_W * WALLPAPER_CACHE_H)
        n = WALLPAPER_CACHE_W * WALLPAPER_CACHE_H;
    for (uint32_t i = 0; i < n; i++) g_wp_old[i] = wallpaper_cache[i];
    g_wp_anim_ms = timer_get_ms();
    g_wp_anim_accent = get_accent_color();
}

 extern const char* get_hostname(void);


#define DESKTOP_COUNT 5
#define MAX_ALL_APPS 32

int gui_running = 1;
static int g_st = 1; // 1=desktop, 2..19=start_menu, 20=control_center, 21=pins_modal, 22=sound_popover
#define MENU_ANIM_MS 300
static uint32_t g_menu_anim_ms = 0;   /* timestamp of last menu open/close start */
static uint32_t last_menu_open_ms = 0; /* debounce: swallow open-click for MENU_ANIM_MS */
static int g_menu_anim_closing = 0;  /* 1 while the close animation plays */

/* ── Taskbar layout (live global) ── */
static layout_t g_layout = {
    1, 26, 255,     /* top, 26px, opaque */
    1,              /* autohide on */
    0,              /* monitor off (not used in Mac bar) */
    0,              /* no clock (not used in Mac bar) */
    0,              /* no toolbar (apps dropdown handles it) */
    0, 0, 0, 0      /* monitor_w, acrylic, glow, search, bell (unused) */
};
layout_t* gui_get_layout(void) { return &g_layout; }

void gui_get_work_area(int* wx, int* wy, int* ww, int* wh) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    if (fw == 0 || fh == 0) { fw = 1024; fh = 768; }
    layout_t* L = &g_layout;
    int t_sz = L->size;
    int x = 0, y = 0, w = (int)fw, h = (int)fh;
    if (L->pos == 0) {        /* bottom */
        h -= t_sz;
    } else if (L->pos == 1) { /* top */
        y += t_sz;
        h -= t_sz;
    } else if (L->pos == 2) { /* left */
        x += t_sz;
        w -= t_sz;
    } else if (L->pos == 3) { /* right */
        w -= t_sz;
    }
    if (wx) *wx = x;
    if (wy) *wy = y;
    if (ww) *ww = w;
    if (wh) *wh = h;
}

/* saved layout presets (4 slots) + names, used by the Layout app */
static layout_t g_layout_presets[4];
static char g_layout_preset_names[4][24] = { "Compact", "Wide", "Side Dock", "Minimal" };

int g_sysmon_cpu = 0, g_sysmon_ram = 0;
static uint32_t g_sysmon_tick = 0;
static uint64_t g_sm_prev_sched = 0, g_sm_prev_busy = 0;

extern size_t kheap_free(void);
#ifndef KHEAP_SIZE
#define KHEAP_SIZE (64 * 1024 * 1024)
#endif

void gui_sample_sysmon(void) {
    uint32_t now = timer_get_ms();
    if (now - g_sysmon_tick < 350 && g_sysmon_tick != 0) return;
    g_sysmon_tick = now;
    uint64_t sched = cpu_sched_ticks;
    uint64_t busy  = cpu_busy_ticks;
    uint64_t ds = sched - g_sm_prev_sched;
    uint64_t db = busy  - g_sm_prev_busy;
    g_sm_prev_sched = sched;
    g_sm_prev_busy = busy;
    int cpu = (ds > 0) ? (int)((db * 100) / ds) : 4;
    if (cpu > 100) cpu = 100; if (cpu < 1) cpu = 1;
    g_sysmon_cpu = cpu;
    size_t free_heap = kheap_free();
    uint64_t used = (KHEAP_SIZE > free_heap) ? (KHEAP_SIZE - free_heap) : 0;
    int ram = (int)((used * 100) / KHEAP_SIZE);
    if (ram > 100) ram = 100; if (ram < 2) ram = 2;
    g_sysmon_ram = ram;
}
static int wallpaper_dirty = 1; // set to 1 to redraw wallpaper next frame
void gui_set_wallpaper_dirty(void) { wallpaper_dirty = 1; }
static int m_idx = 0;
static int prev_mouse_btn = 0;
static int prev_right_btn = 0;     /* edge detector for right button */

static int start_cat_idx = 0; // 0=All, 1=Productivity, 2=System, 3=Games, 4=Graphics, 5=Debug, 6=Accessibility, 7=Networking
static int menu_scroll = 0;
static char start_search_buf[64] = {0};
static int start_search_len = 0;

static char search_buf[64];
static int search_len = 0;
static int search_results[32];
static int search_result_count = 0;
static int search_sel = 0;
static int search_open = 0;
static int search_scroll = 0;

app_item_t menu_app_entries[] = {
    {"Calculator", 0, 1, 0, "calculator"},
    {"File Explorer", 0, 1, 1, "file_explorer"},
    {"Text Editor", 0, 1, 5, "kilo"},
    {"Edrowser", 0, 7, 10, "edrowser"},
    {"Calendar", 0, 1, 96, "calendar"},
    {"Process Viewer", 0, 2, 9, "process_viewer"},
    {"Clock", 0, 1, 97, "clock"},
    {"Terminal", 0, 2, 8, "terminal"},
    {"Julia", 0, 4, 31, "julia"},
    {"Colour Wheel", 0, 4, 33, "colour_wheel"},
    {"Image Viewer", 0, 4, 21, "imgview"},
    {"Bitmap Maker", 0, 4, 22, "bitmap_maker"},
    {"Paint Studio", 0, 4, 54, "paint_studio"},
    {"Graphing Calculator", 0, 4, 24, "graphing"},
    {"Mandelbrot", 0, 4, 6, "mandelbrot"},
    {"GLGears", 0, 4, 46, "glgears"},
    {"GL Demos", 0, 4, 58, "gldemos"},
    {"GLTeapot", 0, 4, 59, "glteapot"},
    {"Kernel Log", 0, 5, 27, "kernellog"},
    {"Weather", 0, 5, 60, "weather"},
    {"Hex Viewer", 0, 5, 7, "hexdump"},
    {"On-Screen Keyboard", 0, 6, 20, "osk"},
    {"Settings", 0, 2, 98, "settings"},
    {"DOOM", 0, 3, 91, "doom"},
    {"File I/O Test", 0, 5, 93, "fiotest"},
    {0, 0, 0, 0}
};


const char* all_items[] = {
    "Calculator", "File Explorer", 
    "Text Editor", "Mandelbrot", "Hex Viewer", "Terminal", "Process Viewer", 
    "Edrowser", "Calendar", "Clock", "Settings",
    "On-Screen Keyboard", 
    "Image Viewer", "Bitmap Maker", "Paint Studio", "Graphing Calculator", "GLGears", "GL Demos", "GLTeapot",
    "Network Debug", "Kernel Log", 
    "Julia", "Colour Wheel",
    "Ring3 Test",
    "ImGui Test",
    "File I/O Test",
    "DOOM",
    "DVD Bounce",
    0
};

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

static void gfx_fill_rect_glass(int x, int y, int w, int h, int r, uint32_t base_color, uint8_t alpha) {
    gfx_fill_rect_alpha(x, y, w, h, base_color, alpha);
    if (r > 0) {
        gfx_fill_rect_alpha(x + r, y, w - 2*r, 2, 0xFFFFFF, 40);
        gfx_draw_rect_rounded_outline(x, y, w, h, r, 1, gfx_lighten(base_color, 35));
    } else {
        gfx_fill_rect_alpha(x, y, w, 2, 0xFFFFFF, 40);
        gfx_draw_rect_outline(x, y, w, h, 1, gfx_lighten(base_color, 35));
    }
}

extern personalization_t* get_personalization(void);
extern uint32_t theme_get_color(theme_role_t role);
extern uint32_t get_accent_color(void);
extern void theme_init(void);

uint32_t get_theme_color(int id) {
    /* Map the legacy CLR_ID_* color IDs to the correct theme roles so the
     * file dialogs / explorer / widget surfaces render dark and theme-aware
     * (the old mapping pointed CLR_ID_BG_DARK at THEME_ROLE_PRIMARY, which is
     * near-white under dark themes -> white dialogs with white text). */
    static const uint32_t legacy_map[] = {
        THEME_ROLE_BACKGROUND,        /* CLR_ID_BG_DARK       */
        THEME_ROLE_SURFACE,           /* CLR_ID_BG_SURFACE    */
        THEME_ROLE_SURFACE_VARIANT,   /* CLR_ID_BG_CARD       */
        THEME_ROLE_SURFACE_VARIANT,   /* CLR_ID_BG_HOVER      */
        THEME_ROLE_OUTLINE,           /* CLR_ID_BORDER        */
        THEME_ROLE_OUTLINE,           /* CLR_ID_BORDER_LIGHT  */
        THEME_ROLE_PRIMARY,           /* CLR_ID_TEXT_PRIMARY    */
        THEME_ROLE_SECONDARY,         /* CLR_ID_TEXT_SECONDARY  */
        THEME_ROLE_TERTIARY,          /* CLR_ID_TEXT_TERTIARY   */
        THEME_ROLE_TERTIARY,          /* CLR_ID_TEXT_DIM        */
        THEME_ROLE_TASKBAR_BG,        /* CLR_ID_TASKBAR        */
        THEME_ROLE_TASKBAR_TEXT,      /* CLR_ID_TASKBAR_GLOW   */
        THEME_ROLE_WINDOW_TITLE,      /* CLR_ID_WINDOW_TITLE   */
        THEME_ROLE_SHADOW,            /* CLR_ID_SHADOW         */
    };
    if (id < 0 || id >= (int)(sizeof(legacy_map)/sizeof(legacy_map[0]))) return 0xFF0000;
    return theme_get_color(legacy_map[id]);
}

void gui_system_shutdown() {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    gfx_fill_rect(0, 0, fw, fh, 0x000000);
    gfx_draw_string_transparent((fw-150)/2, (fh-16)/2, "SYSTEM SHUTDOWN", 0xFFFFFF);
    swap_buffers();
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
    while(1) { __asm__ volatile("hlt"); }
}

/* ═══════════════════════════════════════════════════════
 * Custom geometric sans-serif letterforms for the
 * "EIGEN" desktop watermark.
 * ═══════════════════════════════════════════════════════ */
static void wlp_letter(int ox, int oy, char c, uint32_t rgb) {
#define WR(rx,ry,rw,rh) gfx_fill_rect((ox)+(rx),(oy)+(ry),(rw),(rh),(rgb))
    int W = 28, H = 52, S = 5;
    switch (c) {
    case 'E':
        WR(0, 0, W, S); WR(0, 0, S, H); WR(0, H/2-S/2, W*3/4, S); WR(0, H-S, W, S); break;
    case 'I':
        WR(0, 0, W, S); WR((W-S)/2, 0, S, H); WR(0, H-S, W, S); break;
    case 'G':
        WR(S, 0, W-S, S); WR(0, 0, S, H); WR(S, H-S, W-S, S); WR(W-S, H/2, S, H/2); WR(W/2, H/2-S/2, W/2, S); break;
    case 'N':
        WR(0, 0, S, H); WR(W-S, 0, S, H);
        for (int i = 0; i < H; i++) {
            int dx = i * (W - 2*S) / (H - 1);
            gfx_fill_rect(ox + S + dx, oy + i, S, 1, rgb);
        }
        break;
    default: break;
    }
#undef WR
}

static void wlp_eigen(int x, int y, uint32_t rgb) {
    const char* s = "EIGEN";
    int cx = x;
    for (int i = 0; s[i]; i++) {
        wlp_letter(cx, y, s[i], rgb);
        cx += 28 + 10;
    }
}

/* Blit cached wallpaper to backbuffer — uses a consistent row stride so the
 * cache (written with the real framebuffer stride) maps 1:1 with the BB. */
static void blit_wallpaper_cache(uint32_t fw, uint32_t fh) {
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    if (!bb) return;
    uint32_t cw = (fw < WALLPAPER_CACHE_W) ? fw : WALLPAPER_CACHE_W;
    uint32_t ch = (fh < WALLPAPER_CACHE_H) ? fh : WALLPAPER_CACHE_H;
    /* safety: never walk past the 1920x1080 cache buffer */
    if ((uint64_t)ch * stride > (uint64_t)WALLPAPER_CACHE_W * WALLPAPER_CACHE_H)
        ch = (WALLPAPER_CACHE_W * WALLPAPER_CACHE_H) / stride;
    for (uint32_t y = 0; y < ch; y++) {
        uint32_t* dst = bb + y * stride;
        uint32_t* src = wallpaper_cache + y * stride;
        for (uint32_t x = 0; x < cw; x++) dst[x] = src[x];
    }
}

/* Render wallpaper into cache buffer — called only when wallpaper_dirty==1 */
static void render_wallpaper_to_cache(uint32_t fw, uint32_t fh) {
    /* Render everything into the real back buffer first (gfx_ calls go there) */
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    if (!bb) return;

    personalization_t* p = get_personalization();
    int w_id = p->wallpaper_id;
    if (w_id < 0 || w_id > 4) w_id = 0;

    /* File-mode wallpaper: cover-fit the photo straight into the back buffer
     * (same target as the procedural path below), then the common copy-to-cache
     * at the end snapshots it into wallpaper_cache. We render at the ACTUAL
     * screen size so the whole image is visible and fills the screen (no bars,
     * no letterbox). */
    if (p->wallpaper_mode == 1 && p->wallpaper_file[0]) {
        wallpaper_mgr_blit_active(bb, (int)stride, (int)fw, (int)fh);
    } else {

    /* Base Gradient per Wallpaper Asset */
    uint32_t top_c = 0x141820, bot_c = 0x202530;
    switch (w_id) {
        case 0: top_c = 0x141820; bot_c = 0x202530; break; // Eigen Aurora
        case 1: top_c = 0x050507; bot_c = 0x0E1015; break; // Obsidian Grid
        case 2: top_c = 0x111114; bot_c = 0x1C1C22; break; // Monochrome Waves
        case 3: top_c = 0x0B0F19; bot_c = 0x161D2B; break; // Nordic Midnight
        case 4: top_c = 0x0A0A0C; bot_c = 0x141418; break; // Cyber Matrix
    }

    for (uint32_t py = 0; py < fh; py++) {
        int t = (int)(py * 255 / fh);
        int r = RGB_R(top_c) + ((RGB_R(bot_c) - RGB_R(top_c)) * t / 255);
        int g = RGB_G(top_c) + ((RGB_G(bot_c) - RGB_G(top_c)) * t / 255);
        int b = RGB_B(top_c) + ((RGB_B(bot_c) - RGB_B(top_c)) * t / 255);
        uint32_t pix = gfx_rgb_to_pixel(RGB(r, g, b));
        uint32_t* row = bb + py * stride;
        for (uint32_t px = 0; px < fw; px++) row[px] = pix;
    }

    /* Procedural Assets per Wallpaper */
    if (w_id == 0) {
        /* Eigen Aurora: Silk Streaks */
        struct { int cx_pct; int cy_pct; int rise; int hw; int bright; } streaks[] = {
            { 20, 35, 55, 90, 80  },
            { 45, 52, 55, 50, 110 },
            { 35, 43, 55, 18, 180 },
        };
        for (int si = 0; si < 3; si++) {
            int cx = (int)fw * streaks[si].cx_pct / 100;
            int cy = (int)fh * streaks[si].cy_pct / 100;
            int rise = streaks[si].rise, hw = streaks[si].hw, bright = streaks[si].bright;
            int hw2 = hw * hw;
            for (int py = 0; py < (int)fh; py++) {
                int scx = cx + ((py - cy) * rise / 100);
                int x0 = scx - hw * 3, x1 = scx + hw * 3;
                if (x0 < 0) x0 = 0; if (x1 >= (int)fw) x1 = (int)fw - 1;
                for (int px = x0; px <= x1; px++) {
                    int dist = px - scx; if (dist < 0) dist = -dist;
                    int d2 = dist * dist;
                    if (d2 >= hw2 * 4) continue;
                    int alpha = bright * (hw2 * 4 - d2) / (hw2 * 4);
                    if (alpha > 255) alpha = 255; if (alpha < 2) continue;
                    gfx_blend_pixel(px, py, 0xD0D8E8, (uint8_t)alpha);
                }
            }
        }
    } else if (w_id == 1) {
        /* Obsidian Grid: Sleek 16x16 Slate Geometry Grid */
        for (uint32_t py = 0; py < fh; py += 16) gfx_fill_rect_alpha(0, py, fw, 1, 0x2A2A35, 30);
        for (uint32_t px = 0; px < fw; px += 16) gfx_fill_rect_alpha(px, 0, 1, fh, 0x2A2A35, 30);
        gfx_fill_rect_alpha(0, fh / 2, fw, 1, 0x4A5060, 60);
    } else if (w_id == 2) {
        /* Monochrome Waves: Smooth Layered Ribbons */
        for (int w = 0; w < 4; w++) {
            int base_y = fh * (32 + w * 14) / 100;
            uint32_t w_color = (w == 0 ? 0x888899 : (w == 1 ? 0x666677 : 0x444455));
            uint8_t alpha = 60 - w * 10;
            for (uint32_t px = 0; px < fw; px++) {
                int wave_y = base_y + (int)(16 * (px % 140) / 140) - (int)(16 * ((px + 70) % 140) / 140);
                if (wave_y >= 0 && wave_y < (int)fh) {
                    gfx_fill_rect_alpha(px, wave_y, 1, fh - wave_y, w_color, alpha);
                }
            }
        }
    } else if (w_id == 3) {
        /* Nordic Midnight: Vertical Ambient Light Beams */
        for (int p = 0; p < 5; p++) {
            int px = fw * (12 + p * 18) / 100;
            gfx_fill_rect_alpha(px, 0, 45, fh, 0xE2E8F0, 18);
        }
    } else if (w_id == 4) {
        /* Cyber Matrix: Geometric Dot Matrix */
        for (uint32_t py = 0; py < fh; py += 20) {
            for (uint32_t px = 0; px < fw; px += 20) {
                gfx_blend_pixel(px, py, 0x888888, 40);
            }
        }
    }

    /* FINE PARTICLE DUST */
    static const struct { int x_pct; int y_pct; int a; } dust[] = {
        {8,12,100},{17,7,80},{23,31,90},{33,18,70},{41,9,100},
        {50,24,80},{58,11,90},{63,38,70},{72,16,100},{79,29,80},
        {85,8,90},{91,21,70},{96,35,100},{6,55,60},{14,67,80},
        {28,72,70},{44,80,90},{57,65,60},{71,77,80},{88,60,70},
    };
    int ndust = (int)(sizeof(dust)/sizeof(dust[0]));
    uint32_t dust_col = (w_id == 5 ? 0xE6C280 : (w_id == 1 ? 0xFF90E8 : 0xE6EAF0));
    for (int i = 0; i < ndust; i++) {
        int sx = (int)fw * dust[i].x_pct / 100;
        int sy = (int)fh * dust[i].y_pct / 100;
        if (sy > 0 && sy < (int)fh - TASKBAR_H - 50) {
            gfx_blend_pixel(sx, sy, dust_col, (uint8_t)dust[i].a);
            if (i % 3 == 0) {
                gfx_blend_pixel(sx+1, sy,   dust_col, (uint8_t)(dust[i].a/2));
                gfx_blend_pixel(sx,   sy+1, dust_col, (uint8_t)(dust[i].a/2));
            }
        }
    }

    /* Background Pattern Overlay if set — skip when none */
    if (p->bg_pattern > 0) {
        int s = p->bg_pattern_size > 0 ? p->bg_pattern_size : 1;
        /* Only draw the pattern pixels that need modifying, not full scan */
        for (uint32_t py = 0; py < fh; py++) {
            int ys = (int)(py / s);
            for (uint32_t px = 0; px < fw; px++) {
                int xs = (int)(px / s);
                int hit = 0;
                switch (p->bg_pattern) {
                    case 1:  hit = (xs%8==0||ys%8==0); break;
                    case 2:  hit = ((xs%12)<2&&(ys%12)<2); break;
                    case 3:  hit = (((xs+ys)%16)<2||((xs-ys+100)%16)<2); break;
                    case 4:  hit = ((xs%10)<1||(ys%10)<1); break;
                    case 5:  hit = 1; break;
                    case 6:  hit = (((xs+ys*3)%20)<2); break;
                    case 7:  hit = (((xs/10)+(ys/10))%2); break;
                    case 8:  hit = ((xs%24)<2||(((xs+12)%24)<2&&(ys%14)<7)); break;
                    case 9:  hit = (ys%8==0||(ys%16<8?xs%20==0:(xs+10)%20==0)); break;
                    case 10: hit = ((xs+ys)%14<2); break;
                    case 11: hit = (xs%14==0); break;
                    default: break;
                }
                if (hit) {
                    uint32_t idx = py * stride + px;
                    uint32_t c = bb[idx];
                    if (p->bg_pattern == 5) c = gfx_darken(c, ((xs+ys*2)*7%25));
                    else c = gfx_lighten(c, 18);
                    bb[idx] = c;
                }
            }
        }
    }

    /* EIGEN BRAND WATERMARK — rendered into cache, uses cache coords */
    int wlp_text_w = 5 * 28 + 4 * 10;
    int wlp_text_h = 52;
    int tx = (int)fw - wlp_text_w - 24;
    int ty = (int)fh - TASKBAR_H - wlp_text_h - 14;

    if (tx > 60 && ty > 60) {
        gfx_fill_rect_alpha(tx - 10, ty - 8, wlp_text_w + 20, wlp_text_h + 18, 0x000000, 48);
        wlp_eigen(tx + 2, ty + 2, 0x1E2530);
        wlp_eigen(tx,     ty,     0xD0D8E8);
        gfx_fill_rect(tx, ty + wlp_text_h + 6, wlp_text_w, 3, 0x8899AA);
    }
    } /* end procedural else */

    /* Common: snapshot the back buffer (photo OR procedural) into the cache
     * using the real framebuffer stride, so blit_wallpaper_cache maps 1:1. */
    {
        uint32_t cw = (fw < WALLPAPER_CACHE_W) ? fw : WALLPAPER_CACHE_W;
        uint32_t ch = (fh < WALLPAPER_CACHE_H) ? fh : WALLPAPER_CACHE_H;
        if ((uint64_t)ch * stride > (uint64_t)WALLPAPER_CACHE_W * WALLPAPER_CACHE_H)
            ch = (WALLPAPER_CACHE_W * WALLPAPER_CACHE_H) / stride;
        for (uint32_t y = 0; y < ch; y++) {
            uint32_t* src = bb + y * stride;
            uint32_t* dst = wallpaper_cache + y * stride;
            for (uint32_t x = 0; x < cw; x++) dst[x] = src[x];
        }
    }
}

void draw_premium_wallpaper(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    if (fw == 0 || fh == 0) return;

    if (wallpaper_dirty) {
        /* Render wallpaper once into the cache buffer */
        render_wallpaper_to_cache(fw, fh);
        wallpaper_dirty = 0;
    }

    /* Fast blit: copy cache to back buffer every frame */
    blit_wallpaper_cache(fw, fh);

    /* Wallpaper-apply circle reveal: an expanding circle of the new wallpaper
     * wipes over the previous one, with an accent-coloured ring at the edge. */
    if (g_wp_anim_ms) {
        uint32_t now = timer_get_ms();
        int dt = (int)(now - g_wp_anim_ms);
        if (dt >= WP_ANIM_MS || dt < 0) {
            g_wp_anim_ms = 0;          /* animation finished */
        } else if (g_wp_old) {
            uint32_t stride = gfx_get_stride();
            uint32_t* bb = gfx_get_back_buffer();
            if (bb) {
                uint32_t cw = (fw < WALLPAPER_CACHE_W) ? fw : WALLPAPER_CACHE_W;
                uint32_t ch = (fh < WALLPAPER_CACHE_H) ? fh : WALLPAPER_CACHE_H;
                if ((uint64_t)ch * stride > (uint64_t)WALLPAPER_CACHE_W * WALLPAPER_CACHE_H)
                    ch = (WALLPAPER_CACHE_W * WALLPAPER_CACHE_H) / stride;
                uint32_t cx = fw / 2, cy = fh / 2;
                uint32_t maxR = isqrt_u32(cx * cx + cy * cy);
                uint32_t R = (uint32_t)((uint64_t)maxR * (uint64_t)dt / WP_ANIM_MS);
                uint32_t ringW = 7;
                for (uint32_t y = 0; y < ch; y++) {
                    int dy = (int)y - (int)cy;
                    uint32_t* dst  = bb + y * stride;
                    uint32_t* oldp = g_wp_old + y * stride;
                    uint32_t* newp = wallpaper_cache + y * stride;
                    for (uint32_t x = 0; x < cw; x++) {
                        int dx = (int)x - (int)cx;
                        uint32_t d = isqrt_u32((uint32_t)(dx * dx + dy * dy));
                        if (d <= R) dst[x] = newp[x];
                        else        dst[x] = oldp[x];
                        int dr = (d > R) ? (int)(d - R) : (int)(R - d);
                        if ((uint32_t)dr <= ringW) dst[x] = g_wp_anim_accent;
                    }
                }
            }
        }
    }
}

/* ── Minimal λ Start Icon (no brackets) ── */
static void draw_start_button_emblem(int cx, int cy, uint32_t color) {
    /* Lambda λ — two strokes */
    gfx_draw_line(cx + 2, cy - 7, cx - 5, cy + 7, color);
    gfx_draw_line(cx + 3, cy - 7, cx - 4, cy + 7, color);
    gfx_draw_line(cx - 1, cy + 1, cx + 6, cy + 7, color);
    gfx_draw_line(cx,     cy + 1, cx + 7, cy + 7, color);
}

/* ═══════════════════════════════════════════════════════
 * TASKBAR — Monochrome, Square, Solid, Minimal
 * Honours the live g_layout: position (bottom/top/left/right),
 * thickness, opacity, autohide, live CPU/RAM monitor, clock/toolbar toggle.
 * Right tray: live CPU/RAM graphs + Sound icon + Clock.
 * ═══════════════════════════════════════════════════════ */

/* rolling history of the last N samples for CPU/RAM */
static int g_cpu_hist[120], g_cpu_hi = 0, g_cpu_cnt = 0;
static int g_ram_hist[120], g_ram_hi = 0, g_ram_cnt = 0;
static void push_hist(int* h, int* hi, int* cnt, int v) {
    h[*hi] = v; *hi = (*hi + 1) % 120; if (*cnt < 120) (*cnt)++;
}
/* vertical gradient fill (top col -> bottom col) for the area under a line */
static uint32_t mix_col(uint32_t a, uint32_t b, int t) { /* t 0..255 a->b */
    int ar=(a>>16)&255, ag=(a>>8)&255, ab=a&255;
    int br=(b>>16)&255, bg=(b>>8)&255, bb=b&255;
    int r=ar+((br-ar)*t)/255, g=ag+((bg-ag)*t)/255, bl=ab+((bb-ab)*t)/255;
    return (r<<16)|(g<<8)|bl;
}
/* area-filled line monitor: grid, gradient fill, line, head dot, labels.
   Works in both orientations (th x th gives a horizontal strip when called
   with a wide w and short h, or a tall narrow strip when w<h). */
static void draw_monitor(int x, int y, int w, int h,
                         int* hist, int hi, int cnt,
                         uint32_t line_col, const char* label, int cur) {
    gfx_fill_rect(x, y, w, h, 0x0B0C0F);
    gfx_draw_rect_outline(x, y, w, h, 1, 0x26262B);
    /* grid */
    for (int k = 1; k < 4; k++) {
        int gy = y + (h * k) / 4;
        gfx_draw_hline(x, gy, w, 0x16181D);
    }
    gfx_draw_vline(x + w / 2, y, h, 0x16181D);

    if (cnt < 2) { cur = 0; }
    int ox = x + 1, oy = y + 1, ow = w - 2, oh = h - 2;
    int prev_px = -1, prev_py = -1;
    int last_px = x, last_py = y + oh;
    /* area fill: per-column using linear interp between samples */
    for (int cx2 = ox; cx2 < ox + ow; cx2++) {
        /* map cx2 to sample index (float) */
        int fi = (cnt > 1) ? ((cx2 - ox) * (cnt - 1)) / ow : 0;
        int v0 = hist[(hi - cnt + fi + 120) % 120];
        int v1 = hist[(hi - cnt + fi + 1 + 120) % 120];
        int frac = ((cx2 - ox) * (cnt - 1)) - fi * ow;
        if (frac < 0) frac = 0; if (frac > ow) frac = ow;
        int v = v0 + ((v1 - v0) * frac) / (ow > 0 ? ow : 1);
        if (v > 100) v = 100; if (v < 0) v = 0;
        int py = oy + oh - (v * oh) / 100;
        uint32_t fc = mix_col(line_col, 0x0B0C0F, 200); /* fade to bg */
        gfx_draw_vline(cx2, py, (oy + oh) - py, fc);
    }
    /* line + head dot */
    prev_px = -1; prev_py = -1;
    for (int i = 0; i < cnt; i++) {
        int si = (hi - cnt + i + 120) % 120;
        int v = hist[si]; if (v > 100) v = 100; if (v < 0) v = 0;
        int px = ox + (cnt > 1 ? (i * ow) / (cnt - 1) : ow / 2);
        int py = oy + oh - (v * oh) / 100;
        if (i > 0 && prev_px >= 0) gfx_draw_line(prev_px, prev_py, px, py, line_col);
        prev_px = px; prev_py = py; last_px = px; last_py = py;
    }
    if (cnt >= 1) {
        gfx_fill_circle(last_px, last_py, 2, line_col);
        gfx_fill_rect(last_px, last_py, 1, 1, 0xFFFFFF);
    }
    /* label + current value */
    char lv[10]; snprintf(lv, sizeof lv, "%s %d%%", label, cur);
    gfx_draw_string_transparent(x + 4, y + 3, lv, line_col);
}


void draw_taskbar() {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    if (fw == 0 || fh == 0) return;
    layout_t* L = &g_layout;
    personalization_t* p = get_personalization();
    int tb_tray_start = 0;   /* x where window tabs end (search/bell anchor) */

    push_hist(g_cpu_hist, &g_cpu_hi, &g_cpu_cnt, g_sysmon_cpu);
    push_hist(g_ram_hist, &g_ram_hi, &g_ram_cnt, g_sysmon_ram);

    int vertical = (L->pos == 2 || L->pos == 3);
    int thick = L->size;

    /* geometry */
    int bx, by, bw, bh;
    if (vertical) {
        bw = thick; bh = fh;
        bx = (L->pos == 2) ? 0 : (int)fw - thick;
        by = 0;
    } else {
        bw = fw; bh = thick;
        bx = 0;
        by = (L->pos == 1) ? 0 : (int)fh - thick; /* top or bottom */
    }

    /* autohide: slide the whole bar off-edge unless hovered, with easing */
    int mx = mouse_get_x(), my = mouse_get_y();
    int hidden = 0;
    if (L->autohide) {
        int on = 0;
        if (vertical) on = (mx >= bx - 2 && mx <= bx + bw + 2);
        else          on = (my >= by - 2 && my <= by + bh + 2);
        if (!on) hidden = 1;
    }

    /* animate a 0..1 reveal factor toward the target (1 shown, 0 hidden) */
    static float g_tb_anim = 1.0f;
    static uint32_t g_tb_anim_ms = 0;
    int target = hidden ? 0 : 1;
    uint32_t now = timer_get_ms();
    if (g_tb_anim < (float)target) {
        if (g_tb_anim == 0.0f) g_tb_anim_ms = now;
        float dt = (float)(now - g_tb_anim_ms) / 220.0f;
        if (dt > 1.0f) dt = 1.0f;
        g_tb_anim = dt; /* linear-ish ease; 0->1 */
        if (g_tb_anim > 1.0f) g_tb_anim = 1.0f;
    } else if (g_tb_anim > (float)target) {
        if (g_tb_anim == 1.0f) g_tb_anim_ms = now;
        float dt = (float)(now - g_tb_anim_ms) / 220.0f;
        if (dt > 1.0f) dt = 1.0f;
        g_tb_anim = 1.0f - dt; /* 1->0 */
        if (g_tb_anim < 0.0f) g_tb_anim = 0.0f;
    }
    /* apply a perpendicular slide offset so the bar eases in/out */
    int off = (int)((1.0f - g_tb_anim) * thick);
    if (off > 0) {
        if (vertical)      bx += (L->pos == 2) ? -off : off;
        else               by += (L->pos == 0) ?  off : -off;
    }
    /* when fully hidden, draw nothing (the OS acts as if the bar is absent) */
    if (hidden && g_tb_anim <= 0.001f) return;
    /* a thin always-present reveal strip at the edge when hidden-but-animating */
    if (hidden && g_tb_anim < 1.0f && g_tb_anim > 0.001f) {
        int rx = bx, ry = by, rw = bw, rh = bh;
        if (vertical) rw = 3; else rh = 3;
        if (L->pos == 3) rx = bx + bw - 3;
        if (L->pos == 1) ry = by + bh - 3;
        gfx_fill_rect(rx, ry, rw, rh, 0x2A2A2E);
    }

    /* theme-driven palette */
    uint32_t tb_bg    = theme_get_color(THEME_ROLE_TASKBAR_BG);
    uint32_t tb_sep   = theme_get_color(THEME_ROLE_WINDOW_BORDER);
    uint32_t tb_hover = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t tb_act   = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t tb_txt   = theme_get_color(THEME_ROLE_TASKBAR_TEXT);
    uint32_t tb_mut   = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t cpu_col  = 0x4DA6FF;
    uint32_t ram_col  = 0x7EE787;
    uint32_t acc = get_accent_color();

    /* base (opacity via alpha) */
    gfx_fill_rect_alpha(bx, by, bw, bh, tb_bg, (uint8_t)L->opacity);
    if (L->acrylic) {
        /* soft top sheen for depth */
        if (L->pos == 0 || L->pos == 2) {
            for (int r = 0; r < 3; r++)
                gfx_fill_rect_alpha(bx, by + r, bw, 1, 0xFFFFFF, 12);
        }
    }
    if (vertical) {
        gfx_draw_vline(bx + (L->pos == 2 ? bw - 1 : 0), by, bh, tb_sep);
    } else {
        gfx_draw_hline(bx, by + (L->pos == 0 ? 0 : bh - 1), bw, tb_sep);
    }

    int margin = 4;
    int taskbar_y = by;       /* alias used below for vertical too */
    int taskbar_h = bh;
    (void)taskbar_y;

    /* ── TOOLBAR (start + desktops + pinned + window tabs) ── */
    if (L->show_toolbar) {
        int start_btn_w = vertical ? (bw - 2 * margin) : 36;
        int start_x = vertical ? bx + margin : bx + margin;
        int start_y = vertical ? by + margin : by + 2;
        int start_hover = vertical
            ? point_in_rect(mx, my, start_x, start_y, start_btn_w, start_btn_w)
            : point_in_rect(mx, my, start_x, by + 2, start_btn_w, bh - 4);
        int menu_active = (g_st >= 2 && g_st <= 19);
        uint32_t sbg = (menu_active || start_hover) ? tb_act : tb_bg;
        if (vertical) gfx_fill_rect_alpha(start_x, start_y, start_btn_w, start_btn_w, sbg, (uint8_t)L->opacity);
        else          gfx_fill_rect_alpha(start_x, by + 2, start_btn_w, bh - 4, sbg, (uint8_t)L->opacity);
        if (menu_active)
            gfx_draw_hline(start_x, by + 2, start_btn_w, tb_txt);
        draw_start_button_emblem(start_x + start_btn_w / 2,
                                 (vertical ? start_y : by) + start_btn_w / 2,
                                 menu_active ? 0xFFFFFF : (start_hover ? tb_txt : tb_mut));

        if (vertical) {
            /* desktops + pins + window tabs stacked vertically */
            int dy = start_y + start_btn_w + 6;
            int cell = bw - 2 * margin;
            for (int i = 0; i < DESKTOP_COUNT; i++) {
                int is_cur = (i == wm_get_current_desktop());
                int hov = point_in_rect(mx, my, bx + margin, dy, cell, cell);
                gfx_fill_rect_alpha(bx + margin, dy, cell, cell, (is_cur || hov) ? tb_act : tb_bg, (uint8_t)L->opacity);
                if (is_cur) gfx_draw_hline(bx + margin, dy, cell, tb_txt);
                char num[2] = { '1' + i, 0 };
                gfx_draw_string_transparent(bx + margin + cell / 2 - 4, dy + cell / 2 - 6, num, is_cur ? tb_txt : tb_mut);
                dy += cell + 2;
            }
            dy += 6;
            uint64_t pinned_mask = p->taskbar_pinned_mask;
            for (int i = 0; menu_app_entries[i].name != 0; i++) {
                int g_idx = menu_app_entries[i].global_idx;
                if (!(pinned_mask & (1ULL << g_idx))) continue;
                if (dy + cell > by + bh - margin) break;
                int hov = point_in_rect(mx, my, bx + margin, dy, cell, cell);
                if (hov) gfx_fill_rect_alpha(bx + margin, dy, cell, cell, tb_hover, (uint8_t)L->opacity);
                draw_app_icon(menu_app_entries[i].name, bx + margin + (cell - 22) / 2, dy + (cell - 22) / 2);
                dy += cell + 1;
            }
            /* window tabs as small markers */
            dy += 4;
            for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                wm_window_t* win = wm_get_window_by_index(i);
                if (!win) continue;
                if (dy + 6 > by + bh - margin) break;
                int foc = (win->flags & WM_FLAG_FOCUSED) != 0;
                gfx_fill_rect(bx + margin, dy, cell, 5, foc ? tb_txt : tb_mut);
                dy += 7;
            }
        } else {
            /* Horizontal taskbar — MIRRORED order (LEFT→RIGHT) per spec:
               [start] [desktop switcher] [pinned apps] [loaded apps] [graphs] [date/time] [notification] */
            int mgn = 4;
            int right_edge = (int)fw;
            uint64_t pinned_mask = p->taskbar_pinned_mask;

            /* ===== START opener (FAR LEFT) ===== */
            int start_btn_w = 36;
            int start_x = bx + mgn;
            int start_y = by + 2;
            int start_hover = point_in_rect(mx, my, start_x, start_y, start_btn_w, bh - 4);
            int menu_active = (g_st >= 2 && g_st <= 19);
            uint32_t sbg = (menu_active || start_hover) ? tb_act : tb_bg;
            gfx_fill_rect_alpha(start_x, start_y, start_btn_w, bh - 4, sbg, (uint8_t)L->opacity);
            if (menu_active) gfx_draw_hline(start_x, start_y, start_btn_w, tb_txt);
            draw_start_button_emblem(start_x + start_btn_w/2, start_y + (bh-4)/2,
                                     menu_active ? 0xFFFFFF : (start_hover ? tb_txt : tb_mut));
            int cluster_left = start_x + start_btn_w + 6;
            gfx_draw_vline(cluster_left, by + 6, bh - 12, tb_sep);

            /* ===== DESKTOP switcher (right of start) ===== */
            int desk_w = 24, desk_h = bh - 8;
            int desk_x = cluster_left + 6;
            for (int i = 0; i < DESKTOP_COUNT; i++) {
                int dx = desk_x + i * (desk_w + 2);
                int ddy = by + 4;
                int is_cur = (i == wm_get_current_desktop());
                int hov = point_in_rect(mx, my, dx, ddy, desk_w, desk_h);
                gfx_fill_rect(dx, ddy, desk_w, desk_h, (is_cur || hov) ? tb_act : tb_bg);
                if (is_cur) gfx_draw_hline(dx, ddy, desk_w, tb_txt);
                char num[2] = { '1' + i, 0 };
                gfx_draw_string_transparent(dx + 8, ddy + (desk_h - 12) / 2, num, is_cur ? tb_txt : tb_mut);
            }
            int cluster_left2 = desk_x + DESKTOP_COUNT * (desk_w + 2) + 2;
            gfx_draw_vline(cluster_left2, by + 6, bh - 12, tb_sep);

            /* ===== PINNED apps (right of desktop switcher) ===== */
            int pin_btn_w = 32;
            int pin_x = cluster_left2 + 6;
            int px = pin_x;
            for (int i = 0; menu_app_entries[i].name != 0; i++) {
                int g_idx = menu_app_entries[i].global_idx;
                if (!(pinned_mask & (1ULL << g_idx))) continue;
                const char* aname = menu_app_entries[i].name;
                int hov = point_in_rect(mx, my, px, by + 2, pin_btn_w, bh - 4);
                if (hov) gfx_fill_rect_rounded(px, by + 2, pin_btn_w, bh - 4, 7, tb_hover);
                draw_app_icon(aname, px + (pin_btn_w - 22) / 2, by + 5);
                int running = 0, focused = 0;
                for (int ww = 0; ww < WM_MAX_WINDOWS; ww++) {
                    wm_window_t* win = wm_get_window_by_index(ww);
                    if (win && strstr(win->title, aname)) { running = 1; if (win->flags & WM_FLAG_FOCUSED) focused = 1; }
                }
                if (running) {
                    int dcx = px + pin_btn_w / 2;
                    if (L->glow) {
                        int ph = (now / 320) % 1000;
                        int pp = (ph < 500) ? ph * 200 / 500 : (1000 - ph) * 200 / 500;
                        gfx_fill_rect_alpha(dcx - 3, by + bh - 7, 6, 5, focused ? acc : tb_mut, (uint8_t)(40 + pp / 4));
                    }
                    gfx_fill_rect(dcx - 1, by + bh - 4, 3, 2, focused ? acc : tb_mut);
                }
                px += pin_btn_w + 1;
            }
            int tabs_left = px + 8;
            gfx_draw_vline(tabs_left, by + 6, bh - 12, tb_sep);

            /* ===== LOADED apps (window tabs) ===== */
            int open_wins = 0;
            for (int i = 0; i < WM_MAX_WINDOWS; i++) if (wm_get_window_by_index(i)) open_wins++;
            /* reserve the right side for graphs + date + notification */
            int right_reserved = mgn + 12;
            if (L->monitor) right_reserved += L->monitor_w + 16;   /* two graphs */
            right_reserved += 56 + 12;                            /* date/time */
            right_reserved += 32 + 12;                            /* notification bell */
            int tabs_right = right_edge - right_reserved;
            if (open_wins > 0 && tabs_left < tabs_right) {
                int tab_avail = tabs_right - tabs_left;
                int tab_w = tab_avail / open_wins - 2;
                if (tab_w < 50) tab_w = 50; if (tab_w > 200) tab_w = 200;
                int wx = tabs_left;
                for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                    wm_window_t* win = wm_get_window_by_index(i);
                    if (!win) continue;
                    if (wx + tab_w > tabs_right) break;
                    int is_foc = (win->flags & WM_FLAG_FOCUSED) != 0;
                    int ty2 = by + 3, th2 = bh - 6;
                    int thov = point_in_rect(mx, my, wx, ty2, tab_w, th2);
                    uint32_t tab_bg = is_foc ? tb_act : (thov ? tb_hover : tb_bg);
                    gfx_fill_rect(wx, ty2, tab_w, th2, tab_bg);
                    if (is_foc) gfx_draw_hline(wx, ty2, tab_w, tb_txt);
                    draw_app_icon(win->title, wx + 4, ty2 + (th2 - 22) / 2);
                    if (tab_w > 60) {
                        int mc = (tab_w - 32) / 8; if (mc > 14) mc = 14;
                        int j = 0; char st[16];
                        while (win->title[j] && j < mc) { st[j] = win->title[j]; j++; }
                        st[j] = 0;
                        gfx_draw_string_transparent(wx + 28, ty2 + (th2 - 12) / 2, st, is_foc ? tb_txt : tb_mut);
                    }
                    wx += tab_w + 2;
                }
            }
            int cluster2 = tabs_right + 6;
            gfx_draw_vline(cluster2, by + 6, bh - 12, tb_sep);

            /* ===== GRAPHS (CPU + RAM) ===== */
            int graphs_x = cluster2 + 6;
            if (L->monitor) {
                int g_w = L->monitor_w / 2; if (g_w < 30) g_w = 30;
                int item_h = bh - 8; if (item_h > 30) item_h = 30;
                int mgy = by + (bh - item_h) / 2;
                int g1_x = graphs_x;                 /* CPU */
                int g2_x = graphs_x + g_w + 6;        /* RAM */
                draw_monitor(g1_x, mgy, g_w, item_h, g_cpu_hist, g_cpu_hi, g_cpu_cnt, cpu_col, "CPU", g_sysmon_cpu);
                draw_monitor(g2_x, mgy, g_w, item_h, g_ram_hist, g_ram_hi, g_ram_cnt, ram_col, "RAM", g_sysmon_ram);
                graphs_x = g2_x + g_w + 10;
            }
            gfx_draw_vline(graphs_x, by + 6, bh - 12, tb_sep);

            /* ===== DATE / TIME ===== */
            int clk_x = graphs_x + 6;
            if (L->show_clock) {
                time_t t; get_time(&t);
                char t_str[12], d_str[8];
                if (p->clock_24h) {
                    t_str[0]=(t.hour/10)+'0'; t_str[1]=(t.hour%10)+'0'; t_str[2]=':'; t_str[3]=(t.minute/10)+'0'; t_str[4]=(t.minute%10)+'0'; t_str[5]=0;
                } else {
                    int h12 = t.hour % 12; if (!h12) h12 = 12;
                    t_str[0]=(h12/10)+'0'; t_str[1]=(h12%10)+'0'; t_str[2]=':'; t_str[3]=(t.minute/10)+'0'; t_str[4]=(t.minute%10)+'0';
                    t_str[5]=' '; t_str[6]=(t.hour<12)?'A':'P'; t_str[7]='M'; t_str[8]=0;
                }
                d_str[0]=(t.month/10)+'0'; d_str[1]=(t.month%10)+'0'; d_str[2]='/'; d_str[3]=(t.day/10)+'0'; d_str[4]=(t.day%10)+'0'; d_str[5]=0;
                gfx_draw_string_transparent(clk_x, by + 4,  d_str, tb_mut);
                gfx_draw_string_transparent(clk_x, by + 18, t_str, tb_txt);
                clk_x += 56 + 10;
            }

            /* ===== NOTIFICATION (bell, FAR RIGHT) ===== */
            int bell_w = 32, bell_h = bh - 14, bell_x = right_edge - mgn - bell_w, bell_y = by + (bh - bell_h) / 2;
            int bell_hov = point_in_rect(mx, my, bell_x, bell_y, bell_w, bell_h);
            gfx_fill_rect_rounded(bell_x, bell_y, bell_w, bell_h, 7, bell_hov ? tb_hover : tb_bg);
            {
                int cxp = bell_x + bell_w / 2, cyp = bell_y + bell_h / 2 - 2;
                gfx_fill_circle(cxp, cyp - 1, 5, tb_txt);
                gfx_fill_rect(cxp - 1, cyp + 3, 3, 4, tb_txt);
                gfx_fill_rect(cxp - 3, cyp + 7, 7, 2, tb_txt);
                gfx_fill_circle(cxp + 6, cyp - 6, 4, 0xE5484D);
            }
            int bell_pop_x = bell_x - 334, bell_pop_y = (L->pos == 0) ? by - 8 : by + bh + 8;

            /* ===== Notification popup — Windows 11 style (notification side) ===== */
            static int g_tb_bell_open = 0;
            if (L->bell && bell_hov && wm_mouse_left_clicked()) g_tb_bell_open ^= 1;
            if (g_tb_bell_open) {
                int pw = 340, ph = 420;
                int px = bell_pop_x, py = bell_pop_y - (L->pos == 0 ? ph : 0);
                if (px < 6) px = 6;
                if (px + pw > (int)fw) px = (int)fw - pw - 6;
                gfx_fill_rect_rounded(px, py, pw, ph, 14, 0x1B1B1F);
                gfx_draw_rect_rounded_outline(px, py, pw, ph, 14, 1, 0x3A3A42);
                gfx_fill_rect_rounded(px, py, pw, 44, 14, 0x23232A);
                gfx_draw_string_transparent(px + 18, py + 15, "Notifications", 0xFFFFFF);
                gfx_draw_string_transparent(px + 18, py + 30, "Eigen OS", tb_mut);
                int qy = py + 56, qw = (pw - 36) / 2, qh = 64;
                uint32_t qcols[4] = { 0x39D2C0, 0x4DA6FF, 0x7EE787, 0xF6C500 };
                const char* qlbl[4] = { "Wi-Fi", "Bluetooth", "Focus", "Battery" };
                for (int q = 0; q < 4; q++) {
                    int qx = px + 14 + (q % 2) * (qw + 8);
                    int qyy = qy + (q / 2) * (qh + 8);
                    gfx_fill_rect_rounded(qx, qyy, qw, qh, 10, 0x2A2A31);
                    gfx_fill_rect_rounded(qx + 10, qyy + 10, 26, 26, 7, qcols[q]);
                    gfx_draw_string_transparent(qx + 10, qyy + 42, qlbl[q], 0xD8DEE6);
                }
                int cy = qy + 2 * (qh + 8) + 6;
                gfx_fill_rect_rounded(px + 14, cy, pw - 28, 96, 10, 0x202027);
                gfx_fill_rect_rounded(px + 24, cy + 14, 30, 30, 8, 0x4DA6FF);
                gfx_draw_string_transparent(px + 64, cy + 16, "Eigen OS", 0xFFFFFF);
                gfx_draw_string_transparent(px + 64, cy + 34, "System ready — all services online.", tb_mut);
                gfx_draw_string_transparent(px + 64, cy + 54, "Open Animation Manager from", tb_mut);
                gfx_draw_string_transparent(px + 64, cy + 72, "Customization to tune motion.", tb_mut);
                gfx_draw_string_transparent(px + 14, py + ph - 22, "No other notifications.", tb_mut);
            }
        }
    }

    /* ── RIGHT/LOWER TRAY: monitor + sound + clock ── */
    if (vertical) {
        int ty = by + margin;
        int gw = bw - 2 * margin;
        if (L->monitor) {
            int mh2 = (bh - 2 * margin - 12) / 2; /* two stacked graphs */
            draw_monitor(bx + margin, ty, gw, mh2, g_cpu_hist, g_cpu_hi, g_cpu_cnt, cpu_col, "CPU", g_sysmon_cpu);
            ty += mh2 + 6;
            draw_monitor(bx + margin, ty, gw, mh2, g_ram_hist, g_ram_hi, g_ram_cnt, ram_col, "RAM", g_sysmon_ram);
            ty += mh2 + 8;
        }
        /* clock */
        if (L->show_clock) {
            time_t t; get_time(&t);
            char t_str[12];
            if (p->clock_24h) { t_str[0]=(t.hour/10)+'0'; t_str[1]=(t.hour%10)+'0'; t_str[2]=':'; t_str[3]=(t.minute/10)+'0'; t_str[4]=(t.minute%10)+'0'; t_str[5]=0; }
            else { int h12=t.hour%12; if(!h12)h12=12; t_str[0]=(h12/10)+'0'; t_str[1]=(h12%10)+'0'; t_str[2]=':'; t_str[3]=(t.minute/10)+'0'; t_str[4]=(t.minute%10)+'0'; t_str[5]=0; }
            gfx_draw_string_transparent(bx + margin, ty, t_str, tb_txt);
            ty += 14;
            char d_str[8]; d_str[0]=(t.month/10)+'0'; d_str[1]=(t.month%10)+'0'; d_str[2]='/'; d_str[3]=(t.day/10)+'0'; d_str[4]=(t.day%10)+'0'; d_str[5]=0;
            gfx_draw_string_transparent(bx + margin, ty, d_str, tb_mut);
        }
    } else {
        /* horizontal taskbar content is drawn earlier in this function
           (notification / time / graphs / window tabs / pins / desktops / start).
           Nothing else to draw here for the horizontal case. */
    }
}

/* ─────────────────────────────────────────────────────────────
 * HAIKU START MENU
 *   Left rail of 7 categories (System / Games / Accessibility /
 *   Debug / Graphing / Networking / All Apps). Clicking a rail item
 *   opens a dropdown panel to the right listing that category's apps;
 *   clicking an app launches it.
 * ───────────────────────────────────────────────────────────── */

static void menu_anim_apply(int* oy, int* veil);
static void menu_anim_finalize_close(void);
static int menu_is_animating(void);

static void menu_anim_apply(int* oy, int* veil) {
    float p = gui_anim_progress(g_menu_anim_ms, MENU_ANIM_MS);   /* 0→1 */
    /* ease-out so the motion is snappy then settles */
    float e = p * (2.0f - p);
    if (g_menu_anim_closing) { p = 1.0f - p; e = p * (2.0f - p); }
    /* slide up into place on open (from ~70px below), slide back down on close */
    int slide = 70;
    *oy = (int)((1.0f - e) * (float)slide);
    if (g_menu_anim_closing) *oy = -(int)((1.0f - e) * (float)slide);
    /* veil/opacity fade for a clear open/close feel */
    *veil = (int)((1.0f - e) * 150.0f);
}

static void menu_anim_finalize_close(void) {
    if (g_menu_anim_closing &&
        (int)(timer_get_ms() - g_menu_anim_ms) >= MENU_ANIM_MS) {
        g_menu_anim_closing = 0;
        g_st = 1;
    }
}

/* 1 while the start/menu overlay is mid open/close animation; 0 once it has
   settled. Used to skip the expensive drop-shadow while the menu just sits open. */
static int menu_is_animating(void) {
    if (g_menu_anim_closing) return 1;
    float p = gui_anim_progress(g_menu_anim_ms, MENU_ANIM_MS);
    return p < 1.0f;
}

static void render_menu(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int mw = 560, mh = 450;
    int mx = 10, my = (int)fh - TASKBAR_H - mh - 10;
    if (my < 10) my = 10;

    uint32_t bg_main  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t text_clr = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t sel_bg   = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t hover_bg = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t muted    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t border   = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t acc      = get_accent_color();   /* theme accent — makes the theme visibly apply */
    int mx_m = mouse_get_x(), my_m = mouse_get_y();

    gfx_draw_shadow(mx, my, mw, mh, 25);
    gfx_fill_rect(mx, my, mw, mh, bg_main);
    gfx_draw_rect_outline(mx, my, mw, mh, 1, border);

    /* ── 1. Top Header with hostname ── */
    int header_h = 36;
    gfx_fill_rect(mx, my, mw, header_h, sel_bg);
    gfx_draw_hline(mx, my + header_h, mw, border);
    gfx_fill_rect(mx, my, 3, header_h, acc);   /* accent tab on the header */

    gfx_draw_string_transparent(mx + 12, my + 11, get_hostname(), text_clr);
    gfx_draw_string_transparent(mx + 80, my + 11, "Eigen OS 2.0", muted);

    /* ── 2. Search Bar ── */
    int search_y = my + header_h + 6;
    int search_h = 26;
    gfx_fill_rect(mx + 8, search_y, mw - 16, search_h, hover_bg);
    gfx_draw_rect_outline(mx + 8, search_y, mw - 16, search_h, 1, border);
    draw_search_icon(mx + 14, search_y + 5, 16, 16);

    const char* s_disp = (start_search_len > 0) ? start_search_buf : "Type to filter...";
    gfx_draw_string_transparent(mx + 34, search_y + 7, s_disp, (start_search_len > 0) ? text_clr : muted);

    /* ── 3. Left Category Sidebar ── */
    int side_x = mx + 8;
    int side_y = search_y + search_h + 6;
    int side_w = 120;
    int side_h = mh - (side_y - my) - 8;

    static const char* cat_names[8] = { "All Apps", "Productivity", "System", "Games", "Graphics", "Debug", "Accessibility", "Networking"};
    for (int c = 0; c < 8; c++) {
        int cy = side_y + c * 34;
        int is_sel = (start_cat_idx == c);
        int hover = point_in_rect(mx_m, my_m, side_x, cy, side_w, 28);
        if (is_sel || hover)
            gfx_fill_rect(side_x, cy, side_w, 28, hover ? hover_bg : sel_bg);
        if (is_sel)
            gfx_fill_rect(side_x, cy, 3, 28, acc);   /* accent bar on selected category */
        gfx_draw_string_transparent(side_x + 10, cy + 8, cat_names[c], is_sel ? text_clr : muted);
    }

    gfx_draw_vline(side_x + side_w + 6, side_y, side_h, border);

    /* ── 4. Right App Grid (scrollable) ── */
    int grid_x = side_x + side_w + 12;
    int grid_y = side_y;
    int grid_w = mw - (grid_x - mx) - 8;
    int grid_h = side_h;

    int card_w = 118, card_h = 60;

    int cols = (grid_w - 12) / (card_w + 6);
    if (cols < 1) cols = 1;
    int total_vis = 0;
    if (start_search_len > 0) {
        total_vis = search_result_count;
    } else {
        for (int i = 0; menu_app_entries[i].name != 0; i++) {
            if (start_cat_idx != 0 && menu_app_entries[i].category != start_cat_idx) continue;
            total_vis++;
        }
    }
    int has_scroll = (total_vis * (card_h + 6) > grid_h + 24);
    int scroll_h = has_scroll ? 16 : 0;
    int max_rows = (grid_h - scroll_h) / (card_h + 6);
    int max_vis = max_rows * cols;

    /* Cache the visible grid page so the expensive draw_app_icon calls
       (circles + gradients + shadows) run only when the page changes,
       not every 16ms frame. Render once into a scratch buffer, then
       memcpy it to the screen each frame and overlay the hover. */
    static uint32_t* grid_cache = NULL;
    static int       cache_valid = 0;
    static uint32_t  cache_acc = 0;
    static int       cache_cat = -1, cache_scroll = -1, cache_slen = -1;
    int need_rebuild = !cache_valid
                    || cache_acc != acc
                    || cache_cat != start_cat_idx
                    || cache_scroll != menu_scroll
                    || cache_slen != start_search_len;
    if (need_rebuild) cache_valid = 0;

    if (!cache_valid) {
        int cap = grid_w * grid_h;
        if (!grid_cache) grid_cache = kmalloc(cap * (int)sizeof(uint32_t));
        if (grid_cache) {
            uint32_t* bb = gfx_get_back_buffer();
            uint32_t stride = gfx_get_stride();
            gfx_push_clip(grid_x, grid_y, grid_w, grid_h);
            int vnow = 0;
            for (int i = 0; menu_app_entries[i].name != 0; i++) {
                if (start_search_len > 0) {
                    const char* h = menu_app_entries[i].name;
                    const char* n = start_search_buf;
                    int match = 1;
                    while (*n) {
                        char a = *h, b = *n;
                        if (a >= 'A' && a <= 'Z') a += 32;
                        if (b >= 'A' && b <= 'Z') b += 32;
                        if (a != b) { match = 0; break; }
                        h++; n++;
                    }
                    if (!match) continue;
                } else {
                    if (start_cat_idx != 0 && menu_app_entries[i].category != start_cat_idx) continue;
                }
                if (vnow < menu_scroll) { vnow++; continue; }
                if (vnow >= menu_scroll + max_vis) break;
                int idx = vnow - menu_scroll;
                int r = idx / cols, col = idx % cols;
                int cx2 = grid_x + col * (card_w + 6);
                int cy2 = grid_y + r * (card_h + 6);
                gfx_fill_rect(cx2, cy2, card_w, card_h, bg_main);
                draw_app_icon(menu_app_entries[i].name, cx2 + (card_w - 24) / 2, cy2 + 6);
                char tt[16]; int j = 0;
                while (menu_app_entries[i].name[j] && j < 13) { tt[j] = menu_app_entries[i].name[j]; j++; }
                tt[j] = 0;
                int tw = j * 8;
                gfx_draw_string_transparent(cx2 + (card_w - tw) / 2, cy2 + 38, tt, text_clr);
                vnow++;
            }
            for (int yy = 0; yy < grid_h; yy++) {
                memcpy(&grid_cache[yy * grid_w],
                       &bb[(grid_y + yy) * stride + grid_x],
                       grid_w * sizeof(uint32_t));
            }
            gfx_fill_rect(grid_x, grid_y, grid_w, grid_h, bg_main);
            gfx_pop_clip();
            cache_acc = acc; cache_cat = start_cat_idx;
            cache_scroll = menu_scroll; cache_slen = start_search_len;
        }
        cache_valid = 1;
    }

    /* Blit the cached page. */
    if (grid_cache && cache_valid) {
        uint32_t* bb = gfx_get_back_buffer();
        uint32_t stride = gfx_get_stride();
        for (int yy = 0; yy < grid_h; yy++) {
            memcpy(&bb[(grid_y + yy) * stride + grid_x],
                   &grid_cache[yy * grid_w],
                   grid_w * sizeof(uint32_t));
        }
    }

    /* Overlay hover highlight for the single card under the cursor. */
    {
        int vnow = 0;
        for (int i = 0; menu_app_entries[i].name != 0; i++) {
            if (start_search_len > 0) {
                const char* h = menu_app_entries[i].name;
                const char* n = start_search_buf;
                int match = 1;
                while (*n) {
                    char a = *h, b = *n;
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (b >= 'A' && b <= 'Z') b += 32;
                    if (a != b) { match = 0; break; }
                    h++; n++;
                }
                if (!match) continue;
            } else {
                if (start_cat_idx != 0 && menu_app_entries[i].category != start_cat_idx) continue;
            }
            if (vnow < menu_scroll) { vnow++; continue; }
            if (vnow >= menu_scroll + max_vis) break;
            int idx = vnow - menu_scroll;
            int r = idx / cols, col = idx % cols;
            int cx2 = grid_x + col * (card_w + 6);
            int cy2 = grid_y + r * (card_h + 6);
            if (point_in_rect(mx_m, my_m, cx2, cy2, card_w, card_h)) {
                gfx_fill_rect(cx2, cy2, card_w, card_h, hover_bg);
                draw_app_icon(menu_app_entries[i].name, cx2 + (card_w - 24) / 2, cy2 + 6);
                char tt[16]; int j = 0;
                while (menu_app_entries[i].name[j] && j < 13) { tt[j] = menu_app_entries[i].name[j]; j++; }
                tt[j] = 0;
                int tw = j * 8;
                gfx_draw_string_transparent(cx2 + (card_w - tw) / 2, cy2 + 38, tt, text_clr);
            }
            vnow++;
        }
    }

    /* Scroll arrows */
    if (has_scroll) {
        int arr_y = grid_y + grid_h - 14;
        if (menu_scroll > 0) {
            int ha = point_in_rect(mx_m, my_m, grid_x, arr_y, 20, 12);
            gfx_draw_string_transparent(grid_x, arr_y, "^", ha ? text_clr : muted);
        }
        if (menu_scroll < total_vis - max_vis) {
            int hb = point_in_rect(mx_m, my_m, grid_x + grid_w - 20, arr_y, 20, 12);
            gfx_draw_string_transparent(grid_x + grid_w - 20, arr_y, "v", hb ? text_clr : muted);
        }
    }

    /* Mouse wheel scroll */
    if (has_scroll) {
        int wd = mouse_get_wheel_delta();
        if (wd != 0) {
            mouse_clear_wheel_delta();
            menu_scroll -= wd;
            if (menu_scroll < 0) menu_scroll = 0;
            if (total_vis > max_vis && menu_scroll > total_vis - max_vis)
                menu_scroll = total_vis - max_vis;
            cache_valid = 0; /* page changed: rebuild next frame */
        }
    }

    gfx_pop_clip();
}

/* ═══════════════════════════════════════════════════════
 * TASKBAR PINNED APPS SELECTOR MODAL (g_st == 21)
 * ═══════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════

/* ═══════════════════════════════════════════════════════
 * TASKBAR PINNED APPS SELECTOR MODAL (g_st == 21)
 * ═══════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════
 * SOUND POPOVER PANEL (g_st == 22) — vertical volume editor
 * Opens when the speaker icon in the taskbar is clicked.
 * ═══════════════════════════════════════════════════════ */
static void render_sound_popover(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int pw = 60, ph = 260;
    int px = (int)fw - pw - 10;
    int py = (int)fh - TASKBAR_H - ph - 6;
    if (py < 4) py = 4;

    uint32_t sp_bg   = 0x141416;
    uint32_t sp_bdr  = 0x2A2A2E;
    uint32_t sp_fill = 0xC0C0C4;
    int mx_m = mouse_get_x(), my_m = mouse_get_y();

    gfx_fill_rect_alpha(px + 4, py + 4, pw, ph, 0x000000, 60);
    gfx_fill_rect(px, py, pw, ph, sp_bg);
    gfx_draw_rect_outline(px, py, pw, ph, 1, sp_bdr);

    int vol = audio_get_master_volume();
    int sl_x = px + pw / 2 - 10;
    int sl_top = py + 20;
    int sl_bot = py + ph - 30;
    int sl_h = sl_bot - sl_top;
    int sl_w = 20;

    gfx_fill_rect(sl_x, sl_top, sl_w, sl_h, 0x1C1C20);
    gfx_draw_rect_outline(sl_x, sl_top, sl_w, sl_h, 1, sp_bdr);

    int fill_h = sl_h * vol / 100;
    int fill_y = sl_bot - fill_h;
    if (fill_h > 0)
        gfx_fill_rect(sl_x + 2, fill_y, sl_w - 4, fill_h, sp_fill);

    int handle_y = fill_y - 6;
    if (handle_y < sl_top) handle_y = sl_top;
    if (handle_y > sl_bot - 12) handle_y = sl_bot - 12;
    gfx_fill_rect(sl_x - 4, handle_y, sl_w + 8, 12, 0xE4E4E8);
    gfx_draw_rect_outline(sl_x - 4, handle_y, sl_w + 8, 12, 1, 0xA0A0A4);

    char vstr[8];
    vstr[0] = vol >= 100 ? '1' : (vol >= 10 ? ' ' : ' ');
    vstr[1] = vol >= 100 ? '0' : (vol >= 10 ? ('0' + vol/10) : ('0' + vol));
    vstr[2] = vol >= 100 ? '0' : ('0' + vol % 10);
    vstr[3] = '%'; vstr[4] = 0;
    if (vol >= 100) { vstr[0]='1'; vstr[1]='0'; vstr[2]='0'; vstr[3]='%'; vstr[4]=0; }
    else if (vol >= 10) { vstr[0]='0'+vol/10; vstr[1]='0'+vol%10; vstr[2]='%'; vstr[3]=0; }
    else { vstr[0]='0'+vol; vstr[1]='%'; vstr[2]=0; }
    gfx_draw_string_transparent(px + (pw - 16)/2, sl_bot + 4, vstr, 0x8B949E);

    static int sl_dragging = 0;
    int mbtn2 = mouse_get_buttons();
    if ((mbtn2 & 1)) {
        if (point_in_rect(mx_m, my_m, sl_x - 6, sl_top, sl_w + 12, sl_h + 12)) {
            sl_dragging = 1;
        }
        if (sl_dragging) {
            int rel = sl_bot - my_m;
            if (rel < 0) rel = 0;
            if (rel > sl_h) rel = sl_h;
            audio_set_master_volume(rel * 100 / sl_h);
        }
    } else {
        sl_dragging = 0;
    }
}

/* Draw a small taskbar layout preview thumbnail (px,py = top-left, pw x ph) */
static void draw_layout_preview(int px, int py, int pw, int ph, const layout_t* L, uint32_t acc) {
    /* screen backdrop */
    gfx_fill_rect(px, py, pw, ph, 0x0A0A0D);
    gfx_draw_rect_rounded_outline(px, py, pw, ph, 6, 1, 0x232329);
    int tb = (int)((L->size) * (ph - 16) / 96) + 6;  /* scaled thickness */
    if (tb < 6) tb = 6; if (tb > ph - 8) tb = ph - 8;
    uint32_t tcol = 0x1A1A20;
    gfx_fill_rect_alpha(px + 4, py + 4, pw - 8, ph - 8, tcol, (uint8_t)L->opacity);
    if (L->pos == 0) { /* bottom */
        gfx_fill_rect(px + 3, py + ph - tb - 3, pw - 6, tb, 0x2A2A33);
        /* pinned dots */
        for (int i = 0; i < 6; i++) gfx_fill_rect(px + 8 + i*((pw-16)/6), py + ph - tb - 3 + tb/2 - 3, 6, 6, i==0?acc:0x4A4A55);
    } else if (L->pos == 1) { /* top */
        gfx_fill_rect(px + 3, py + 3, pw - 6, tb, 0x2A2A33);
        for (int i = 0; i < 6; i++) gfx_fill_rect(px + 8 + i*((pw-16)/6), py + 3 + tb/2 - 3, 6, 6, i==0?acc:0x4A4A55);
    } else if (L->pos == 2) { /* left */
        gfx_fill_rect(px + 3, py + 3, tb, ph - 6, 0x2A2A33);
        for (int i = 0; i < 5; i++) gfx_fill_rect(px + 3 + tb/2 - 3, py + 8 + i*((ph-16)/5), 6, 6, i==0?acc:0x4A4A55);
    } else { /* right */
        gfx_fill_rect(px + pw - tb - 3, py + 3, tb, ph - 6, 0x2A2A33);
        for (int i = 0; i < 5; i++) gfx_fill_rect(px + pw - tb - 3 + tb/2 - 3, py + 8 + i*((ph-16)/5), 6, 6, i==0?acc:0x4A4A55);
    }
    if (L->autohide) {
        gfx_draw_string_transparent(px + 6, py + 6, "auto", 0x6E7681);
    }
    if (L->monitor) {
        int mw = (L->pos==2||L->pos==3)? tb-8 : 22;
        gfx_fill_rect(px + pw - mw - 8, py + ph - (L->pos==0?tb+8:8) - (L->pos==0?0:0), mw, 6, 0x4DA6FF);
    }
}

static void render_taskbar_pins_modal(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int mw = 520, mh = 420;
    int mx = (int)(fw - mw) / 2;
    int my = (int)(fh - mh) / 2 - 20;
    if (my < 10) my = 10;

    personalization_t* p = get_personalization();
    uint32_t bg_main  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t text_clr = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t head_bg  = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t hover_bg = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t muted    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t border   = theme_get_color(THEME_ROLE_OUTLINE);
    int mx_m = mouse_get_x(), my_m = mouse_get_y();

    gfx_draw_shadow(mx, my, mw, mh, 25);
    gfx_fill_rect(mx, my, mw, mh, bg_main);
    gfx_draw_rect_outline(mx, my, mw, mh, 1, border);

    gfx_draw_string_transparent(mx + 16, my + 14, "SELECT TASKBAR PINNED APPS", text_clr);
    gfx_draw_string_transparent(mx + 16, my + 30, "Click an app to pin/unpin it on the taskbar", muted);

    int close_hover = point_in_rect(mx_m, my_m, mx + mw - 32, my + 12, 22, 22);
    gfx_fill_rect(mx + mw - 32, my + 12, 22, 22, close_hover ? hover_bg : bg_main);
    gfx_draw_string_transparent(mx + mw - 25, my + 16, "X", muted);

    gfx_draw_hline(mx + 12, my + 48, mw - 24, border);

    int app_count = 0;
    while (menu_app_entries[app_count].name != 0) app_count++;

    int card_w = 216, card_h = 46;
    int cols = 2;
    int rows_per_page = (mh - 100) / (card_h + 8);
    int total_pages = (app_count + cols * rows_per_page - 1) / (cols * rows_per_page);
    static int pins_page = 0;
    if (pins_page >= total_pages && total_pages > 0) pins_page = 0;

    int start_idx = pins_page * cols * rows_per_page;
    int end_idx = start_idx + cols * rows_per_page;
    if (end_idx > app_count) end_idx = app_count;

    int start_y = my + 60;
    for (int i = start_idx; i < end_idx; i++) {
        int idx = i - start_idx;
        int col = idx % cols;
        int row = idx / cols;
        int cx = mx + 16 + col * (card_w + 16);
        int cy = start_y + row * (card_h + 8);

        int g_idx = menu_app_entries[i].global_idx;
        int is_pinned = (p->taskbar_pinned_mask & (1ULL << g_idx)) != 0;
        int hov = point_in_rect(mx_m, my_m, cx, cy, card_w, card_h);

        gfx_fill_rect(cx, cy, card_w, card_h, is_pinned ? head_bg : (hov ? hover_bg : bg_main));
        gfx_draw_rect_outline(cx, cy, card_w, card_h, 1, is_pinned ? text_clr : border);

        draw_app_icon(menu_app_entries[i].name, cx + 8, cy + 11);

        char app_trunc[16];
        int j = 0;
        while (menu_app_entries[i].name[j] && j < 11) { app_trunc[j] = menu_app_entries[i].name[j]; j++; }
        app_trunc[j] = 0;
        gfx_draw_string_transparent(cx + 38, cy + 16, app_trunc, text_clr);
    }

    // Mouse wheel scroll for pagination
    if (total_pages > 1) {
        int wd = mouse_get_wheel_delta();
        if (wd != 0) {
            mouse_clear_wheel_delta();
            pins_page -= wd;
            if (pins_page < 0) pins_page = 0;
            if (pins_page >= total_pages) pins_page = total_pages - 1;
        }
    }

    if (total_pages > 1) {
        char pg[16];
        snprintf(pg, sizeof(pg), "Page %d/%d", pins_page + 1, total_pages);
        int pg_x = mx + (mw - (int)strlen(pg) * 8) / 2;
        gfx_draw_string_transparent(pg_x, my + mh - 38, pg, muted);

        int arr_w = 20, arr_y = my + mh - 40;
        int l_arr = point_in_rect(mx_m, my_m, mx + 12, arr_y, arr_w, arr_w);
        int r_arr = point_in_rect(mx_m, my_m, mx + mw - 32, arr_y, arr_w, arr_w);
        gfx_draw_string_transparent(mx + 18, arr_y + 4, "<", l_arr ? text_clr : muted);
        gfx_draw_string_transparent(mx + mw - 26, arr_y + 4, ">", r_arr ? text_clr : muted);
    }

    int btn_w = 120, btn_h = 32;
    int btn_x = mx + (mw - btn_w) / 2;
    int btn_y = my + mh - 42;
    int done_hov = point_in_rect(mx_m, my_m, btn_x, btn_y, btn_w, btn_h);
    gfx_fill_rect(btn_x, btn_y, btn_w, btn_h, done_hov ? muted : text_clr);
    gfx_draw_rect_outline(btn_x, btn_y, btn_w, btn_h, 1, border);
    gfx_draw_string_transparent(btn_x + 42, btn_y + 9, "Done", bg_main);
}

/* ═══════════════════════════════════════════════════════
 * TASKBAR LAYOUT MODAL (g_st == 23) — 3 design presets w/ live previews.
 * Also a scrollable pinned-apps list (the same mask the pins modal edits).
 * ═══════════════════════════════════════════════════════ */
/* Open the Taskbar Layout designer modal (g_st == 23). Called from the
 * Personalization app "Taskbar Layout" tab and elsewhere. */
void gui_open_taskbar_layout(void) { g_st = 23; }

static void render_taskbar_layout_modal(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int mw = 600, mh = 460;
    int mx = (int)(fw - mw) / 2;
    int my = (int)(fh - mh) / 2 - 20;
    if (my < 10) my = 10;

    personalization_t* p = get_personalization();
    uint32_t bg_main  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t text_clr = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t head_bg  = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t hover_bg = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t muted    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t border   = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t acc      = get_accent_color();
    int mx_m = mouse_get_x(), my_m = mouse_get_y();
    int mbtn = mouse_get_buttons();

    gfx_draw_shadow(mx, my, mw, mh, 25);
    gfx_fill_rect(mx, my, mw, mh, bg_main);
    gfx_draw_rect_outline(mx, my, mw, mh, 1, border);

    gfx_draw_string_transparent(mx + 16, my + 14, "TASKBAR LAYOUT", text_clr);
    gfx_draw_string_transparent(mx + 16, my + 30, "Pick a design preset, or fine-tune below", muted);

    int close_hover = point_in_rect(mx_m, my_m, mx + mw - 32, my + 12, 22, 22);
    gfx_fill_rect(mx + mw - 32, my + 12, 22, 22, close_hover ? hover_bg : bg_main);
    gfx_draw_string_transparent(mx + mw - 25, my + 16, "X", muted);
    gfx_draw_hline(mx + 12, my + 48, mw - 24, border);

    /* 3 preset cards */
    const int pc_w = (mw - 16*2 - 16*2) / 3;
    const int pc_h = 132;
    const int pc_y = my + 60;
    for (int i = 0; i < 3; i++) {
        int pcx = mx + 16 + i*(pc_w + 16);
        int active = (g_layout.pos==gui_layout_preset(i)->pos &&
                      g_layout.size==gui_layout_preset(i)->size &&
                      g_layout.opacity==gui_layout_preset(i)->opacity);
        int hov = point_in_rect(mx_m, my_m, pcx, pc_y, pc_w, pc_h);
        gfx_fill_rect_rounded(pcx, pc_y, pc_w, pc_h, 10, hov?hover_bg:(bg_main));
        gfx_draw_rect_rounded_outline(pcx, pc_y, pc_w, pc_h, 10, 1, active?acc:border);
        draw_layout_preview(pcx + 10, pc_y + 10, pc_w - 20, 66, gui_layout_preset(i), acc);
        gfx_draw_string_transparent(pcx + 10, pc_y + 82, gui_layout_preset_name(i), text_clr);
        /* Apply button */
        int ab_w = pc_w - 20, ab_h = 24, ab_x = pcx + 10, ab_y = pc_y + pc_h - 30;
        int ab_hov = point_in_rect(mx_m, my_m, ab_x, ab_y, ab_w, ab_h);
        gfx_fill_rect_rounded(ab_x, ab_y, ab_w, ab_h, 6, active?acc:(ab_hov?head_bg:0x1C1C22));
        gfx_draw_string_transparent(ab_x + ab_w/2 - 22, ab_y + 7, active?"Active":"Apply",
                                    active?0xFFFFFF:(ab_hov?0xFFFFFF:0xC8C8D0));
        (void)mbtn;
        if ((mbtn & 1) && point_in_rect(mx_m, my_m, pcx, pc_y, pc_w, pc_h)) {
            gui_apply_layout(gui_layout_preset(i));
        }
    }

    /* fine-tune sliders */
    int sy = pc_y + pc_h + 16;
    gfx_draw_string_transparent(mx + 16, sy, "Position", muted);
    const char* posn[4]={"Bottom","Top","Left","Right"};
    for (int i=0;i<4;i++){
        int bx=mx+16+i*110;
        int on=(g_layout.pos==i);
        int hov=point_in_rect(mx_m,my_m,bx,sy+16,100,24);
        gfx_fill_rect_rounded(bx,sy+16,100,24,6,on?acc:(hov?hover_bg:0x16161C));
        gfx_draw_string_transparent(bx+18,sy+22,posn[i],on?0xFFFFFF:0xC8C8D0);
        if ((mbtn&1)&&hov) g_layout.pos=i;
    }
    gfx_draw_string_transparent(mx + 16, sy+50, "Thickness", muted);
    int sx=mx+16, sw=300, ssh=8, ssy=sy+72;
    gfx_fill_rect(sx, ssy, sw, ssh, 0x1C1C22);
    int kn = (g_layout.size-24)*sw/72;
    gfx_fill_rect(sx, ssy, kn, ssh, acc);
    gfx_fill_rect(sx+kn-5, ssy-4, 10, ssh+8, 0xE4E4E8);
    if ((mbtn&1)&&point_in_rect(mx_m,my_m,sx,ssy-6,sw,ssh+12)) {
        int v=mx_m-sx; if(v<0)v=0; if(v>sw)v=sw; g_layout.size=24+v*72/sw;
    }
    char sval[8]; snprintf(sval,sizeof(sval),"%d px",g_layout.size);
    gfx_draw_string_transparent(sx+sw+10, ssy-2, sval, muted);

    /* scrollable pinned apps (right column) */
    int list_x = mx + mw - 200, list_y = sy, list_w = 184, list_h = mh - (list_y - my) - 16;
    gfx_draw_string_transparent(list_x, list_y, "Pinned Apps", muted);
    int app_count = 0; while (menu_app_entries[app_count].name) app_count++;
    static int layout_pins_scroll = 0;
    int row_h = 30;
    int vis_rows = (list_h - 18) / row_h;
    int max_scroll = app_count*row_h - vis_rows*row_h; if (max_scroll<0) max_scroll=0;
    int wd = mouse_get_wheel_delta();
    if (wd != 0) { mouse_clear_wheel_delta(); layout_pins_scroll -= wd*row_h;
        if (layout_pins_scroll<0) layout_pins_scroll=0; if (layout_pins_scroll>max_scroll) layout_pins_scroll=max_scroll; }
    gfx_draw_rect_outline(list_x, list_y+14, list_w, list_h-14, 1, border);
    for (int i=0;i<app_count;i++){
        int ry = list_y + 16 + i*row_h - layout_pins_scroll;
        if (ry + row_h < list_y + 14) continue;
        if (ry > list_y + list_h) break;
        int g_idx = menu_app_entries[i].global_idx;
        int on = (p->taskbar_pinned_mask & (1ULL<<g_idx))!=0;
        int hov = point_in_rect(mx_m,my_m,list_x+2,ry,list_w-4,row_h-2);
        if (hov) gfx_fill_rect_rounded(list_x+2,ry,list_w-4,row_h-2,5,hover_bg);
        draw_app_icon(menu_app_entries[i].name, list_x+6, ry+5);
        char t[12]; int j=0; while(menu_app_entries[i].name[j]&&j<9){t[j]=menu_app_entries[i].name[j];j++;} t[j]=0;
        gfx_draw_string_transparent(list_x+30, ry+9, t, on?text_clr:0xB0B0B8);
        if ((mbtn&1)&&hov) { if(on) p->taskbar_pinned_mask &= ~(1ULL<<g_idx);
                             else p->taskbar_pinned_mask |= (1ULL<<g_idx); }
    }

    /* Dock/Apply hint */
    gfx_draw_string_transparent(mx + 16, my + mh - 16, "Tip: Super+D toggles the Dock", muted);
}

/* ═══════════════════════════════════════════════════════
 * SPOTLIGHT SEARCH OVERLAY
 * ═══════════════════════════════════════════════════════ */
static void update_search_results(void) {
    search_result_count = 0;
    if (search_len == 0) {
        for (int i = 0; all_items[i] && search_result_count < 32; i++)
            search_results[search_result_count++] = i;
    } else {
        for (int i = 0; all_items[i] && search_result_count < 32; i++) {
            const char *h = all_items[i], *n = search_buf; int m = 1;
            while(*n) {
                char a=*h, b=*n;
                if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32;
                if(a!=b){m=0;break;} h++;n++;
            }
            if(m) search_results[search_result_count++] = i;
        }
    }
    search_sel = 0; search_scroll = 0;
}

static void render_search_panel(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int mw = 600, mh = 400;
    int mx = (fw - mw) / 2;
    int my = (fh - mh) / 2 - 40;

    uint32_t bg_main  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t text_clr = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t field_bg = theme_get_color(THEME_ROLE_MENU_BG);   /* input field = neutral surface, not the saturated selected color */
    uint32_t sel_bg   = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED); /* selected result row */
    uint32_t muted    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t border   = theme_get_color(THEME_ROLE_OUTLINE);

    /* Cache the static parts (icons + row backgrounds) of the result strip
       so we don't re-run draw_app_icon (circles + gradients + shadows) on
       every 16ms frame. Only rebuild when the result set or selection
       changes — i.e., when the user types or navigates results. The blinking
       cursor + selection highlight are drawn on top each frame (cheap). */
    static uint32_t* icon_strip = NULL;
    static int       strip_valid = 0;
    static int       last_buf0 = 0, last_slen = -1, last_cnt = -1, last_sel = -1, last_scr = -1;
    int results_changed = (search_len != last_slen)
                       || (search_result_count != last_cnt)
                       || (search_result_count > 0
                           && search_results[0] != last_buf0)
                       || search_sel != last_sel
                       || search_scroll != last_scr;

    int input_h = 60;
    int ry = my + input_h + 20;
    int show_count = 6;

    /* Rebuild the cached strip when the result set changes. */
    if (results_changed) {
        strip_valid = 0;
        last_slen = search_len;
        last_cnt = search_result_count;
        last_buf0 = search_result_count > 0 ? search_results[0] : -1;
        last_sel = search_sel;
        last_scr = search_scroll;
    }

    if (!strip_valid) {
        if (!icon_strip) {
            icon_strip = kmalloc(mw * show_count * 48 * sizeof(uint32_t));
        }
        if (icon_strip) {
            uint32_t* bb = gfx_get_back_buffer();
            uint32_t stride = gfx_get_stride();
            /* Render the strip (icons + row backgrounds) into the cache by
               drawing directly then copying that rectangle out. */
            int clip_h = mh - (ry - my) - 10;
            int strip_w = mw;
            int strip_h = show_count * 48;
            if (strip_h > clip_h) strip_h = clip_h;

            gfx_push_clip(mx + 10, ry, mw - 20, clip_h);
            for (int i = 0; i < show_count && (i + search_scroll) < search_result_count; i++) {
                int idx = i + search_scroll;
                int iy = ry + i * 48;
                /* row bg (unselected; selection drawn on top each frame) */
                gfx_fill_rect(mx + 10, iy, mw - 36, 44, bg_main);
                draw_app_icon(all_items[search_results[idx]], mx + 24, iy + 10);
                gfx_draw_string_transparent(mx + 60, iy + 16,
                    all_items[search_results[idx]], muted);
            }
            /* copy the strip region out, then clear it on screen so we can
               blit the cache back cleanly and redraw only the highlight. */
            for (int yy = 0; yy < strip_h; yy++) {
                memcpy(&icon_strip[yy * strip_w],
                       &bb[(ry + yy) * stride + (mx + 10)],
                       mw * sizeof(uint32_t));
            }
            /* erase the just-rendered strip on screen */
            for (int i = 0; i < show_count && (i + search_scroll) < search_result_count; i++) {
                int iy = ry + i * 48;
                gfx_fill_rect(mx + 10, iy, mw - 36, 44, bg_main);
            }
            gfx_pop_clip();
        }
        strip_valid = 1;
    }

    gfx_draw_shadow(mx, my, mw, mh, 40);
    gfx_fill_rect(mx, my, mw, mh, bg_main);
    gfx_draw_rect_outline(mx, my, mw, mh, 1, border);

    int input_h_local = 60;
    (void)input_h_local;
    gfx_fill_rect(mx + 10, my + 10, mw - 20, input_h, field_bg);
    gfx_draw_rect_outline(mx + 10, my + 10, mw - 20, input_h, 1, border);

    draw_search_icon(mx + 22, my + 28, 20, 20);

    char display_buf[64];
    int start_pos = (search_len > 50) ? search_len - 50 : 0;
    int k = 0;
    while(k < 50 && search_buf[start_pos + k]) { display_buf[k] = search_buf[start_pos + k]; k++; }
    display_buf[k] = 0;

    gfx_draw_string_transparent(mx + 48, my + 26, display_buf, text_clr);

    /* blinking cursor */
    if ((timer_get_ms() / 500) % 2 == 0) {
        int cx = mx + 48 + k * 8;
        if (cx < mx + mw - 30) gfx_fill_rect(cx, my + 24, 2, 16, text_clr);
    }

    if (search_len == 0) {
        gfx_draw_string_transparent(mx + 48, my + 26, "Type to search applications & files...", muted);
    }

    if (search_sel >= search_scroll + show_count) search_scroll = search_sel - show_count + 1;
    if (search_sel < search_scroll) search_scroll = search_sel;

    /* Blit the cached icon strip, then redraw just the selected row's
       highlight + label (selected label is brighter). */
    if (icon_strip && strip_valid) {
        uint32_t* bb = gfx_get_back_buffer();
        uint32_t stride = gfx_get_stride();
        int strip_w = mw;
        int strip_h = show_count * 48;
        int clip_h = mh - (ry - my) - 10;
        if (strip_h > clip_h) strip_h = clip_h;
        for (int yy = 0; yy < strip_h; yy++) {
            memcpy(&bb[(ry + yy) * stride + (mx + 10)],
                   &icon_strip[yy * strip_w],
                   mw * sizeof(uint32_t));
        }
    } else {
        /* fallback: render directly (first paint / strip rebuild failure) */
        gfx_push_clip(mx + 10, ry, mw - 20, mh - (ry - my) - 10);
        for (int i = 0; i < show_count && (i + search_scroll) < search_result_count; i++) {
            int idx = i + search_scroll;
            int iy = ry + i * 48;
            int is_sel = (search_sel == idx);
            gfx_fill_rect(mx + 10, iy, mw - 36, 44, is_sel ? sel_bg : bg_main);
            gfx_draw_rect_outline(mx + 10, iy, mw - 36, 44, 1, is_sel ? text_clr : border);
            draw_app_icon(all_items[search_results[idx]], mx + 24, iy + 10);
            gfx_draw_string_transparent(mx + 60, iy + 16, all_items[search_results[idx]],
                                        is_sel ? text_clr : muted);
        }
        if (search_result_count == 0 && search_len > 0) {
            gfx_draw_string_transparent(mx + mw / 2 - 60, ry + 40, "No matching applications found", muted);
        }
        gfx_pop_clip();
    }

    /* selection highlight on top of the cached strip */
    if (icon_strip && strip_valid) {
        int idx = search_sel - search_scroll;
        if (idx >= 0 && idx < show_count) {
            int iy = ry + idx * 48;
            if ((search_scroll + idx) < search_result_count) {
                gfx_fill_rect(mx + 10, iy, mw - 36, 44, sel_bg);
                gfx_draw_rect_outline(mx + 10, iy, mw - 36, 44, 1, text_clr);
                draw_app_icon(all_items[search_results[search_scroll + idx]], mx + 24, iy + 10);
                gfx_draw_string_transparent(mx + 60, iy + 16,
                    all_items[search_results[search_scroll + idx]], text_clr);
            }
        }
    }
}

/* launch_item() is keyed by global_idx. The search results store indices into
 * the flat all_items[] list (whose order is unrelated to global_idx), so we
 * must translate the name -> global_idx here. This is what fixes "clicking a
 * search result opens the wrong app". */
static void launch_search_result(int all_items_index) {
    if (all_items_index < 0 || !all_items[all_items_index]) return;
    int g = app_name_to_global_idx(all_items[all_items_index]);
    if (g >= 0) launch_item(g);
}

void launch_item(int global_idx) {
    g_st = 1; search_open = 0; g_menu_anim_closing = 0;
    for (int i = 0; menu_app_entries[i].name != 0; i++) {
        if (menu_app_entries[i].global_idx == global_idx) {
            int before = wm_window_count();
            if (menu_app_entries[i].user_elf) {
                create_user_process_elf(menu_app_entries[i].user_elf);
            } else if (menu_app_entries[i].launch_func) {
                menu_app_entries[i].launch_func();
                if (wm_window_count() == before) {
                    kerror("Could not open \"%s\" — the app didn't start a window.",
                           menu_app_entries[i].name ? menu_app_entries[i].name : "?");
                }
            }
            return;
        }
    }
}

int app_name_to_global_idx(const char* name) {
    for (int i = 0; menu_app_entries[i].name != 0; i++) {
        if (strcmp(menu_app_entries[i].name, name) == 0)
            return menu_app_entries[i].global_idx;
    }
    return -1;
}

/* ring3 user apps: extern declared here (task.c) */
extern int create_user_process_elf(const char* name);

/* ── Taskbar Layout persistence ─────────────────────────────
   Line 1: live layout   pos size opacity autohide monitor clk toolbar mon_w
   Lines: saved presets  name|pos size opacity autohide monitor clk toolbar mon_w */
static void layout_default(layout_t* l) {
    l->pos = TASKBAR_DEFAULT_POS; l->size = TASKBAR_DEFAULT_SIZE;
    l->opacity = TASKBAR_DEFAULT_OPAC; l->autohide = TASKBAR_DEFAULT_AH;
    l->monitor = TASKBAR_DEFAULT_MON; l->show_clock = TASKBAR_DEFAULT_CLK;
    l->show_toolbar = TASKBAR_DEFAULT_TB; l->monitor_w = 120;
    l->acrylic = 1; l->glow = 1; l->search = 1; l->bell = 1;
}

static void layout_write_all(const layout_t* live, const layout_t presets[4],
                             const char pnames[4][24]) {
    const char* path = "cfg/layout.cfg";
    fs_mkdir("cfg");            /* ensure dir exists (resolves under current_dir) */
    fs_delete(path);
    int fd = fs_create(path);
    if (fd < 0) return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%d %d %d %d %d %d %d %d %d %d %d %d\n",
                     live->pos, live->size, live->opacity, live->autohide,
                     live->monitor, live->show_clock, live->show_toolbar, live->monitor_w,
                     live->acrylic, live->glow, live->search, live->bell);
    fs_write(fd, buf, n);
    for (int i = 0; i < 4; i++) {
        n = snprintf(buf, sizeof(buf), "%s|%d %d %d %d %d %d %d %d %d %d %d %d\n",
                     pnames[i], presets[i].pos, presets[i].size, presets[i].opacity,
                     presets[i].autohide, presets[i].monitor, presets[i].show_clock,
                     presets[i].show_toolbar, presets[i].monitor_w,
                     presets[i].acrylic, presets[i].glow, presets[i].search, presets[i].bell);
        fs_write(fd, buf, n);
    }
    fs_close(fd);
}

void gui_apply_layout(const layout_t* l) {
    layout_t c = *l;
    if (c.size < 24) c.size = 24; if (c.size > 96) c.size = 96;
    if (c.opacity < 64) c.opacity = 64; if (c.opacity > 255) c.opacity = 255;
    if (c.monitor_w < 60) c.monitor_w = 60; if (c.monitor_w > 320) c.monitor_w = 320;
    if (c.pos < 0) c.pos = 0; if (c.pos > 3) c.pos = 0;
    g_layout = c;
    layout_write_all(&g_layout, g_layout_presets, g_layout_preset_names);
}

void gui_reset_layout(void) {
    layout_default(&g_layout);
    layout_write_all(&g_layout, g_layout_presets, g_layout_preset_names);
}

layout_t* gui_layout_preset(int i) {
    if (i < 0 || i > 3) return &g_layout_presets[0];
    return &g_layout_presets[i];
}
const char* gui_layout_preset_name(int i) {
    if (i < 0 || i > 3) return g_layout_preset_names[0];
    return g_layout_preset_names[i];
}
void gui_layout_save_preset(int i, const layout_t* l, const char* name) {
    if (i < 0 || i > 3 || !l) return;
    g_layout_presets[i] = *l;
    if (name) {
        int j = 0;
        while (j < 23 && name[j]) { g_layout_preset_names[i][j] = name[j]; j++; }
        g_layout_preset_names[i][j] = 0;
    }
    layout_write_all(&g_layout, g_layout_presets, g_layout_preset_names);
}

void gui_load_layout(void) {
    layout_default(&g_layout);
    for (int i = 0; i < 4; i++) g_layout_presets[i] = g_layout;
    /* Seed 3 distinct design presets the first time (fresh cfg) */
    g_layout_preset_names[0][0] = 0;
    /* preset 0: Classic Bottom */
    g_layout_presets[0].pos = 0; g_layout_presets[0].size = 40;
    g_layout_presets[0].opacity = 255; g_layout_presets[0].autohide = 0;
    g_layout_presets[0].monitor = 1; g_layout_presets[0].show_clock = 1;
    g_layout_presets[0].show_toolbar = 1; g_layout_presets[0].monitor_w = 120;
    { int j=0; const char* s="Classic Bottom"; while(j<23&&s[j]){g_layout_preset_names[0][j]=s[j];j++;} g_layout_preset_names[0][j]=0; }
    /* preset 1: Side Dock (left, slim, autohide) */
    g_layout_presets[1].pos = 2; g_layout_presets[1].size = 56;
    g_layout_presets[1].opacity = 235; g_layout_presets[1].autohide = 1;
    g_layout_presets[1].monitor = 1; g_layout_presets[1].show_clock = 1;
    g_layout_presets[1].show_toolbar = 1; g_layout_presets[1].monitor_w = 110;
    { int j=0; const char* s="Side Dock"; while(j<23&&s[j]){g_layout_preset_names[1][j]=s[j];j++;} g_layout_preset_names[1][j]=0; }
    /* preset 2: Floating Pill (bottom, slim, translucent) */
    g_layout_presets[2].pos = 0; g_layout_presets[2].size = 44;
    g_layout_presets[2].opacity = 180; g_layout_presets[2].autohide = 0;
    g_layout_presets[2].monitor = 0; g_layout_presets[2].show_clock = 1;
    g_layout_presets[2].show_toolbar = 1; g_layout_presets[2].monitor_w = 110;
    { int j=0; const char* s="Floating Pill"; while(j<23&&s[j]){g_layout_preset_names[2][j]=s[j];j++;} g_layout_preset_names[2][j]=0; }
    const char* path = "cfg/layout.cfg";
    int fd = fs_open(path, 0);
    if (fd < 0) return;
    char buf[512]; int total = 0;
    int r = fs_read(fd, buf, sizeof(buf) - 1); fs_close(fd);
    if (r <= 0) return;
    buf[r] = 0;
    /* parse line by line */
    int line = 0;
    for (int i = 0; i <= r && line < 5; ) {
        int e = i;
        while (e < r && buf[e] != '\n') e++;
        int len = (e < r) ? e - i : r - i;
        char linebuf[128]; int ll = len < 127 ? len : 127;
        for (int k = 0; k < ll; k++) linebuf[k] = buf[i + k];
        linebuf[ll] = 0;
        /* split name (if present) */
        char* bar = 0;
        for (int k = 0; k < ll; k++) if (linebuf[k] == '|') { bar = &linebuf[k]; break; }
        char* nums = linebuf;
        if (bar) { *bar = 0; nums = bar + 1; }
        int v[12];
        int cnt = 0;
        char* tok = nums;
        for (int k = 0; k < 12 && *tok; ) {
            while (*tok == ' ' || *tok == '\t') tok++;
            if (!*tok) break;
            int acc = 0, got = 0;
            while (*tok >= '0' && *tok <= '9') { acc = acc * 10 + (*tok - '0'); tok++; got = 1; }
            if (got) v[cnt++] = acc;
            while (*tok && *tok != ' ' && *tok != '\t') tok++;
        }
        /* default the newer visual flags (acrylic/glow/search/bell) to ON
           when an older cfg has fewer than 12 fields */
        int v_acrylic = (cnt > 8) ? v[8] : 1;
        int v_glow    = (cnt > 9) ? v[9] : 1;
        int v_search  = (cnt > 10) ? v[10] : 1;
        int v_bell    = (cnt > 11) ? v[11] : 1;
        if (cnt >= 8) {
            if (line == 0) {
                g_layout.pos = v[0]; g_layout.size = v[1]; g_layout.opacity = v[2];
                g_layout.autohide = v[3]; g_layout.monitor = v[4]; g_layout.show_clock = v[5];
                g_layout.show_toolbar = v[6]; g_layout.monitor_w = v[7];
                g_layout.acrylic = v_acrylic; g_layout.glow = v_glow;
                g_layout.search = v_search; g_layout.bell = v_bell;
            } else {
                int pi = line - 1;
                if (pi < 4) {
                    g_layout_presets[pi].pos = v[0]; g_layout_presets[pi].size = v[1];
                    g_layout_presets[pi].opacity = v[2]; g_layout_presets[pi].autohide = v[3];
                    g_layout_presets[pi].monitor = v[4]; g_layout_presets[pi].show_clock = v[5];
                    g_layout_presets[pi].show_toolbar = v[6]; g_layout_presets[pi].monitor_w = v[7];
                    g_layout_presets[pi].acrylic = v_acrylic; g_layout_presets[pi].glow = v_glow;
                    g_layout_presets[pi].search = v_search; g_layout_presets[pi].bell = v_bell;
                    if (bar) {
                        int nl = 0; while (bar > linebuf + nl && linebuf[nl] && nl < 23) nl++;
                        int j = 0; while (j < 23 && linebuf[j]) { g_layout_preset_names[pi][j] = linebuf[j]; j++; }
                        g_layout_preset_names[pi][j] = 0;
                    }
                }
            }
        }
        line++;
        i = (e < r) ? e + 1 : r + 1;
    }
}

void gui_toggle_start_menu(void) {
    search_open = 0;
    if (g_st >= 2 && g_st <= 19) {
        if (g_menu_anim_closing) {
            /* Was in middle of closing animation: reverse & re-open */
            g_menu_anim_closing = 0;
            g_menu_anim_ms = timer_get_ms();
        } else {
            /* begin close animation; keep g_st in 2..19 until it finishes */
            g_menu_anim_closing = 1;
            g_menu_anim_ms = timer_get_ms();
        }
    } else {
        g_st = 2;
        g_menu_anim_closing = 0;
        g_menu_anim_ms = timer_get_ms();
        last_menu_open_ms = g_menu_anim_ms;
        start_cat_idx = 0; start_search_len = 0; start_search_buf[0] = 0;
    }
}

void gui_toggle_mute(void) { audio_set_mute(!audio_is_muted()); }
int gui_is_audio_hovered(int x, int y, int w, int h) { return point_in_rect(x, y, x, y, w, h); }
void gui_open_search(void) { g_st = 1; search_open = 1; search_len = 0; search_buf[0] = 0; search_sel = 0; update_search_results(); last_menu_open_ms = timer_get_ms(); }
void gui_toggle_search(void) { if (search_open) search_open = 0; else gui_open_search(); }
int gui_is_menu_open(void) { return (g_st >= 2 && g_st <= 19); }
int gui_is_search_open(void) { return search_open; }
int gui_is_overlay_open(void) { return (g_st >= 2 && g_st <= 22) || search_open; }
int gui_is_topbar_cfg_open(void) { return 0; }
void gui_handle_topbar_cfg_key(char key) { (void)key; }

void gui_handle_menu_key(char key_in) {
    unsigned char key = (unsigned char)key_in;
    if (g_st < 2 || g_st > 19) return;
    if (key == 27) { g_st = 1; }
    else if (key == '\b') {
        if (start_search_len > 0) { start_search_buf[--start_search_len] = 0; }
    } else if (key >= 32 && key <= 126 && start_search_len < 60) {
        start_search_buf[start_search_len++] = (char)key;
        start_search_buf[start_search_len] = 0;
    }
}

void gui_handle_search_key(char key_in) {
    unsigned char key = (unsigned char)key_in; if (!search_open) return;
    if (key == 27) { search_open = 0; return; }
    else if (KEY_MATCH(key, '\b')) { if (search_len > 0) { search_buf[--search_len] = 0; update_search_results(); } }
    else if (KEY_MATCH(key, KEY_UP)) { if (search_sel > 0) search_sel--; }
    else if (KEY_MATCH(key, KEY_DOWN)) { if (search_sel < search_result_count - 1) search_sel++; }
    else if (key == '\n' && search_result_count > 0) launch_search_result(search_results[search_sel]);
    else if (key >= 32 && key <= 126 && search_len < 62) { search_buf[search_len++] = (char)key; search_buf[search_len] = 0; update_search_results(); }
}

/* ════════════════════════════════════════════════════════════════════════
 * DESKTOP ICON SYSTEM
 * 2-column grid on the left.  Icons snap to cells.  Drag to reorder.
 * ════════════════════════════════════════════════════════════════════════ */
#define DESK_ICON_W   88   /* cell width  (px) */
#define DESK_ICON_H   96   /* cell height (px) */
#define DESK_PAD_X    12   /* left edge of grid */
#define DESK_PAD_Y    20   /* top  edge of grid */
#define DESK_GRID_C   2    /* number of columns */
#define DESK_TILE_SZ  48   /* icon glyph tile   */
#define DESK_MAX_ITEMS 64

typedef struct {
    int is_file;
    char name[48];
    char path[128];
    int  global_idx;
    int  slot;          /* grid slot index (row*DESK_GRID_C+col), -1 = auto */
} desk_item_t;

static desk_item_t g_desk_items[DESK_MAX_ITEMS];
static int g_desk_item_count = 0;

static int g_desk_sel = -1;
static int g_desk_last_click_idx  = -1;
static uint32_t g_desk_last_click_ms = 0;

/* Drag state */
static int g_desk_drag     = -1;   /* item index being dragged (-1 = none) */
static int g_desk_drag_mx  = 0;    /* current mouse x during drag          */
static int g_desk_drag_my  = 0;    /* current mouse y during drag          */
static int g_desk_drag_ox  = 0;    /* mouse offset inside the icon (x)     */
static int g_desk_drag_oy  = 0;    /* mouse offset inside the icon (y)     */
static int g_desk_drag_x   = 0;    /* press x (for double-click detection) */
static int g_desk_drag_y   = 0;    /* press y (for double-click detection) */

/* Right-click context menu */
static int g_desk_menu = 0;        /* 0=closed 1=item 2=empty-desktop      */
static int g_desk_menu_x = 0, g_desk_menu_y = 0;
static int g_desk_menu_idx = -1;

/* ── helpers ──────────────────────────────────────────────────────────── */

static void desk_shorten_name(const char* full, char* out, int maxc) {
    if (!full) { out[0] = 0; return; }
    int len = 0; while (full[len]) len++;
    if (len <= maxc) { snprintf(out, 48, "%s", full); return; }
    const char* dot = strrchr(full, '.');
    if (dot && (full + len - dot) <= 4) {
        int ext = (int)(full + len - dot);
        int sm  = maxc - 3 - ext; if (sm < 2) sm = 2;
        char stem[32]; strncpy(stem, full, sm); stem[sm] = 0;
        snprintf(out, 48, "%s...%s", stem, dot);
    } else {
        char stem[32]; strncpy(stem, full, maxc - 3); stem[maxc - 3] = 0;
        snprintf(out, 48, "%s...", stem);
    }
}

/* pixel origin of grid slot s */
static void desk_slot_xy(int s, int* ox, int* oy) {
    int col = s % DESK_GRID_C;
    int row = s / DESK_GRID_C;
    *ox = DESK_PAD_X + col * DESK_ICON_W;
    *oy = DESK_PAD_Y + row * DESK_ICON_H;
}

/* nearest valid slot for a pixel coordinate */
static int desk_nearest_slot(int px, int py, uint32_t fh) {
    int col = (px - DESK_PAD_X + DESK_ICON_W / 2) / DESK_ICON_W;
    int row = (py - DESK_PAD_Y + DESK_ICON_H / 2) / DESK_ICON_H;
    int max_rows = ((int)fh - TASKBAR_H - DESK_PAD_Y) / DESK_ICON_H;
    if (col < 0) col = 0;
    if (col >= DESK_GRID_C) col = DESK_GRID_C - 1;
    if (row < 0) row = 0;
    if (row >= max_rows) row = max_rows - 1;
    return row * DESK_GRID_C + col;
}

/* item at slot s, -1 if empty */
static int desk_item_at_slot(int s) {
    for (int k = 0; k < g_desk_item_count; k++)
        if (g_desk_items[k].slot == s) return k;
    return -1;
}

/* assign slots top-to-bottom left-to-right to any item whose slot == -1 */
static void desk_auto_layout(uint32_t fh) {
    int max_rows = ((int)fh - TASKBAR_H - DESK_PAD_Y) / DESK_ICON_H;
    int s = 0;
    for (int k = 0; k < g_desk_item_count; k++) {
        if (g_desk_items[k].slot >= 0) continue;
        while (s < max_rows * DESK_GRID_C && desk_item_at_slot(s) >= 0) s++;
        if (s < max_rows * DESK_GRID_C) g_desk_items[k].slot = s++;
    }
}

/* ── item list refresh ─────────────────────────────────────────────────── */
static void desk_refresh_items(void) {
    /* Save old slots by name so they survive refresh */
    char  old_name[DESK_MAX_ITEMS][48];
    int   old_slot[DESK_MAX_ITEMS];
    int   old_n = g_desk_item_count;
    for (int i = 0; i < old_n; i++) {
        snprintf(old_name[i], 48, "%s", g_desk_items[i].name);
        old_slot[i] = g_desk_items[i].slot;
    }

    g_desk_item_count = 0;
    personalization_t* p = get_personalization();
    uint64_t mask = p->desktop_icons_mask;

    /* 1. App shortcuts */
    for (int i = 0; all_items[i] && g_desk_item_count < DESK_MAX_ITEMS; i++) {
        int gi = app_name_to_global_idx(all_items[i]);
        if (gi >= 0 && (mask & (1ULL << gi))) {
            desk_item_t* it = &g_desk_items[g_desk_item_count];
            it->is_file = 0;
            strncpy(it->name, all_items[i], 47);
            it->path[0] = 0;
            it->global_idx = gi;
            it->slot = -1;
            /* restore saved slot */
            for (int j = 0; j < old_n; j++)
                if (strcmp(old_name[j], it->name) == 0) { it->slot = old_slot[j]; break; }
            g_desk_item_count++;
        }
    }

    /* 2. Pinned files in /home/desktop */
    ensure_dir_chain("/home/desktop");
    int dt_dir = -1;
    for (int i = 0; i < 256; i++) {
        char n[64]; int s=0,t=0,par=-1; uint8_t fl=0; uint32_t mt=0;
        if (fs_get_node(i,n,&s,&t,&par,&fl,&mt)==0 && t==1 && strcmp(n,"desktop")==0)
            { dt_dir = i; break; }
    }
    if (dt_dir >= 0) {
        for (int i = 0; i < 256 && g_desk_item_count < DESK_MAX_ITEMS; i++) {
            char n[64]; int s=0,t=0,par=-1; uint8_t fl=0; uint32_t mt=0;
            if (fs_get_node(i,n,&s,&t,&par,&fl,&mt)==0 && t==0 && par==dt_dir) {
                desk_item_t* it = &g_desk_items[g_desk_item_count];
                it->is_file = 1;
                strncpy(it->name, n, 47);
                snprintf(it->path, 128, "/home/desktop/%s", n);
                it->global_idx = -1;
                it->slot = -1;
                for (int j = 0; j < old_n; j++)
                    if (strcmp(old_name[j], it->name) == 0) { it->slot = old_slot[j]; break; }
                g_desk_item_count++;
            }
        }
    }
}

/* ── file icon draw ────────────────────────────────────────────────────── */
/* small decode-once thumbnail cache for desktop .bmp file icons */
static char    g_desk_thumb_name[2][64];
static uint8_t g_desk_thumb_px[2][40 * 30 * 3];
static int     g_desk_thumb_ok[2];

static void draw_desk_file_icon(const char* name, int gx, int gy) {
    int is_img = (strstr(name,".bmp") || strstr(name,".BMP"));
    if (is_img) {
        /* try a real thumbnail of the picture (decode once per file name) */
        int slot = -1;
        for (int s = 0; s < 2; s++)
            if (g_desk_thumb_ok[s] && strcmp(g_desk_thumb_name[s], name) == 0) { slot = s; break; }
        if (slot < 0) {
            bmp_image_t img; img.pixels = 0;
            if (wallpaper_mgr_decode_file(name, &img) && img.width > 0 && img.height > 0) {
                int iw = img.width, ih = img.height;
                slot = (g_desk_thumb_ok[0] && !g_desk_thumb_ok[1]) ? 1 : 0;
                strncpy(g_desk_thumb_name[slot], name, 63);
                for (int yy = 0; yy < 30; yy++) {
                    int sy = yy * ih / 30;
                    const uint8_t* row = img.pixels + (long)sy * iw * 3;
                    uint8_t* d = g_desk_thumb_px[slot] + yy * 40 * 3;
                    for (int xx = 0; xx < 40; xx++) {
                        int sx = xx * iw / 40;
                        const uint8_t* p = row + sx * 3;
                        d[xx * 3] = p[0]; d[xx * 3 + 1] = p[1]; d[xx * 3 + 2] = p[2];
                    }
                }
                g_desk_thumb_ok[slot] = 1;
            }
            bmp_free(&img);
        }
        gfx_fill_rect_rounded(gx+8, gy+4, 32, 36, 4, 0x1E293B);
        gfx_draw_rect_rounded_outline(gx+8, gy+4, 32, 36, 4, 1, 0x38BDF8);
        if (slot >= 0) {
            /* cover-fit the cached thumbnail into the card */
            int iw = 40, ih = 30, cw = 26, ch = 30;
            int sw = cw, sh = ch, ox = 0, oy = 0;
            if ((long)ch * iw >= (long)cw * ih) { sh = ch; sw = (int)((long)ch * iw / ih); ox = (cw - sw) / 2; }
            else                                { sw = cw; sh = (int)((long)cw * ih / iw); oy = (ch - sh) / 2; }
            for (int yy = 0; yy < ch; yy++) {
                int sy = (yy - oy) * ih / sh;
                if (sy < 0) sy = 0; else if (sy >= ih) sy = ih - 1;
                const uint8_t* row = g_desk_thumb_px[slot] + sy * iw * 3;
                uint32_t* d = &((uint32_t*)gfx_get_back_buffer())[(gy + 4 + 3 + yy) * gfx_get_stride() + (gx + 8 + 3)];
                for (int xx = 0; xx < cw; xx++) {
                    int sx = (xx - ox) * iw / sw;
                    if (sx < 0) sx = 0; else if (sx >= iw) sx = iw - 1;
                    const uint8_t* p = row + sx * 3;
                    d[xx] = 0xFF000000U | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                }
            }
        } else {
            gfx_fill_gradient_v(gx+11, gy+7, 26, 30, 0x0284C7, 0x0F172A);
            gfx_fill_circle(gx+18, gy+14, 3, 0xFACC15);
            gfx_fill_triangle(gx+12, gy+32, gx+22, gy+22, gx+30, gy+32, 0x22C55E);
        }
    } else {
        /* white page with a dark outline so it stays visible on
         * bright photo wallpapers */
        gfx_fill_rect_rounded(gx+10, gy+2, 28, 38, 4, 0xF1F5F9);
        gfx_draw_rect_rounded_outline(gx+10, gy+2, 28, 38, 4, 1, 0x1E293B);
        /* folded corner */
        gfx_fill_triangle(gx+30, gy+2, gx+38, gy+10, gx+30, gy+10, 0xB0BAC8);
        /* text lines */
        gfx_fill_rect(gx+14, gy+14, 14, 2, 0x2563EB);
        gfx_fill_rect(gx+14, gy+19, 20, 2, 0x94A3B8);
        gfx_fill_rect(gx+14, gy+24, 16, 2, 0x94A3B8);
        gfx_fill_rect(gx+14, gy+29, 18, 2, 0x94A3B8);
    }
}

/* ── draw one icon (without the cell bg) ──────────────────────────────── */
static void desk_draw_icon_contents(int k, int ox, int oy, int sel, uint32_t acc) {
    int gx = ox + (DESK_ICON_W - DESK_TILE_SZ) / 2;
    int gy = oy + 6;
    if (g_desk_items[k].is_file) {
        draw_desk_file_icon(g_desk_items[k].name, gx, gy);
    } else {
        draw_app_icon(g_desk_items[k].name,
                      gx + (DESK_TILE_SZ - 24) / 2,
                      gy + (DESK_TILE_SZ - 24) / 2);
    }
    char sn[20];
    desk_shorten_name(g_desk_items[k].name, sn, 10);
    int lw = (int)strlen(sn) * 8;
    int lx = ox + (DESK_ICON_W - lw) / 2;
    int ly = oy + 6 + DESK_TILE_SZ + 4;
    gfx_draw_string_transparent(lx+1, ly+1, sn, 0x000000);
    gfx_draw_string_transparent(lx,   ly,   sn, sel ? 0xFFFFFF : 0xDDE0E8);
    (void)acc;
}

/* ── render ───────────────────────────────────────────────────────────── */
void desktop_tick(void) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int mx = mouse_get_x(), my = mouse_get_y();
    uint32_t acc = get_accent_color();
    (void)fw;

    desk_refresh_items();
    if (g_desk_item_count <= 0) { g_desk_sel = -1; return; }
    desk_auto_layout(fh);

    /* ghost drop-target slot highlight */
    if (g_desk_drag >= 0) {
        int tgt = desk_nearest_slot(mx - g_desk_drag_ox + DESK_ICON_W/2,
                                    my - g_desk_drag_oy + DESK_ICON_H/2, fh);
        int tx, ty; desk_slot_xy(tgt, &tx, &ty);
        /* dashed outline for target cell */
        gfx_fill_rect_alpha(tx+2, ty+2, DESK_ICON_W-4, DESK_ICON_H-4, acc, 30);
        gfx_draw_rect_rounded_outline(tx+2, ty+2, DESK_ICON_W-4, DESK_ICON_H-4, 8, 1, acc);
    }

    /* draw all non-dragged icons */
    for (int k = 0; k < g_desk_item_count; k++) {
        if (k == g_desk_drag) continue; /* draw dragged one last (on top) */
        int s = g_desk_items[k].slot;
        if (s < 0) continue;
        int ox, oy; desk_slot_xy(s, &ox, &oy);
        int tbh = g_layout.size;
        int blocked;
        if (g_layout.pos == 1)          blocked = (oy < tbh + 4);
        else if (g_layout.pos == 0)     blocked = ((uint32_t)(oy + DESK_ICON_H) > fh - tbh - 4);
        else                            blocked = 0;
        if (blocked) continue;

        int hov = (mx >= ox && mx < ox + DESK_ICON_W &&
                   my >= oy && my < oy + DESK_ICON_H);
        int sel = (k == g_desk_sel);

        if (sel) {
            gfx_fill_rect_alpha(ox+2, oy+2, DESK_ICON_W-4, DESK_ICON_H-4, acc, 55);
            gfx_draw_rect_rounded_outline(ox+2, oy+2, DESK_ICON_W-4, DESK_ICON_H-4, 8, 1, acc);
        } else if (hov) {
            gfx_fill_rect_alpha(ox+2, oy+2, DESK_ICON_W-4, DESK_ICON_H-4, 0xFFFFFF, 18);
        }
        desk_draw_icon_contents(k, ox, oy, sel, acc);
    }

    /* draw dragged icon floating under cursor */
    if (g_desk_drag >= 0 && g_desk_drag < g_desk_item_count) {
        int ox = mx - g_desk_drag_ox;
        int oy = my - g_desk_drag_oy;
        /* subtle shadow */
        gfx_fill_rect_alpha(ox+4, oy+6, DESK_ICON_W, DESK_ICON_H, 0x000000, 60);
        /* semi-transparent tint */
        gfx_fill_rect_alpha(ox, oy, DESK_ICON_W, DESK_ICON_H, acc, 30);
        desk_draw_icon_contents(g_desk_drag, ox, oy, 1, acc);
    }

    /* Context menu */
    if (g_desk_menu > 0) {
        int mw = 170, mh = (g_desk_menu == 1) ? 72 : 96;
        int mx0 = g_desk_menu_x, my0 = g_desk_menu_y;
        if (mx0 + mw > (int)fw) mx0 = (int)fw - mw - 4;
        if (my0 + mh > (int)fh - TASKBAR_H) my0 = (int)fh - TASKBAR_H - mh - 4;
        gfx_fill_rect_rounded(mx0, my0, mw, mh, 8, 0x14141A);
        gfx_draw_rect_rounded_outline(mx0, my0, mw, mh, 8, 1, 0x2A2A33);
        if (g_desk_menu == 1 && g_desk_menu_idx >= 0) {
            int ih = 32;
            int oh = (mx>=mx0&&mx<=mx0+mw&&my>=my0+4&&my<my0+ih);
            int rh = (mx>=mx0&&mx<=mx0+mw&&my>=my0+ih+2&&my<my0+mh-4);
            gfx_fill_rect_rounded(mx0+4, my0+4,     mw-8, ih-4, 6, oh ? acc : 0x1C1C22);
            gfx_draw_string_transparent(mx0+14, my0+10,  "> Open",    oh?0xFFFFFF:0xD0D0D8);
            gfx_fill_rect_rounded(mx0+4, my0+ih+2,  mw-8, ih-4, 6, rh ? 0x3A1E1E : 0x1C1C22);
            gfx_draw_string_transparent(mx0+14, my0+ih+8,
                g_desk_items[g_desk_menu_idx].is_file ? "x Delete File"
                                                      : "x Hide icon",
                rh ? 0xFFB4B4 : 0xD0D0D8);
        } else if (g_desk_menu == 2) {
            int ih = 28;
            int h1=(mx>=mx0&&mx<=mx0+mw&&my>=my0+4&&my<my0+4+ih);
            int h2=(mx>=mx0&&mx<=mx0+mw&&my>=my0+4+ih&&my<my0+4+ih*2);
            int h3=(mx>=mx0&&mx<=mx0+mw&&my>=my0+4+ih*2&&my<my0+mh-4);
            gfx_fill_rect_rounded(mx0+4,my0+4,        mw-8,ih-2,6,h1?acc:0x1C1C22);
            gfx_draw_string_transparent(mx0+10,my0+9,          "+ New Text File",   h1?0xFFFFFF:0xD0D0D8);
            gfx_fill_rect_rounded(mx0+4,my0+4+ih,     mw-8,ih-2,6,h2?acc:0x1C1C22);
            gfx_draw_string_transparent(mx0+10,my0+4+ih+5,     "* Personalize...",  h2?0xFFFFFF:0xD0D0D8);
            gfx_fill_rect_rounded(mx0+4,my0+4+ih*2,   mw-8,ih-2,6,h3?acc:0x1C1C22);
            gfx_draw_string_transparent(mx0+10,my0+4+ih*2+5,   "o Sort Icons",      h3?0xFFFFFF:0xD0D0D8);
        }
    }
}

/* ── hit test ─────────────────────────────────────────────────────────── */
static int desk_hit(int cx, int cy, uint32_t fh) {
    for (int k = 0; k < g_desk_item_count; k++) {
        int s = g_desk_items[k].slot; if (s < 0) continue;
        int ox, oy; desk_slot_xy(s, &ox, &oy);
        int tbh = g_layout.size;
        int blocked;
        if (g_layout.pos == 1)          blocked = (oy < tbh + 4);
        else if (g_layout.pos == 0)     blocked = ((uint32_t)(oy + DESK_ICON_H) > fh - tbh - 4);
        else                            blocked = 0;
        if (blocked) continue;
        if (cx >= ox && cx < ox + DESK_ICON_W && cy >= oy && cy < oy + DESK_ICON_H)
            return k;
    }
    return -1;
}

/* ── open item ────────────────────────────────────────────────────────── */
static void desk_open_item(int k) {
    if (k < 0 || k >= g_desk_item_count) return;
    if (g_desk_items[k].is_file) {
        const char* p = g_desk_items[k].path;
        if (strstr(p,".bmp") || strstr(p,".BMP"))
            launch_item(app_name_to_global_idx("Image Viewer"));
        else
            launch_item(app_name_to_global_idx("Text Editor"));
    } else {
        if (g_desk_items[k].global_idx >= 0)
            launch_item(g_desk_items[k].global_idx);
    }
}

/* ── click handler ────────────────────────────────────────────────────── */
static int desktop_click(int cx, int cy, int clicked, int rclicked) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();

    if (rclicked) {
        int k = desk_hit(cx, cy, fh);
        if (k >= 0) {
            g_desk_sel = k; g_desk_menu = 1;
            g_desk_menu_x = cx; g_desk_menu_y = cy; g_desk_menu_idx = k;
        } else {
            g_desk_menu = 2;
            g_desk_menu_x = cx; g_desk_menu_y = cy; g_desk_menu_idx = -1;
        }
        return 1;
    }

    /* Context menu click handling */
    if (g_desk_menu > 0) {
        int mw = 170, mh = (g_desk_menu == 1) ? 72 : 96;
        int mx0 = g_desk_menu_x, my0 = g_desk_menu_y;
        if (mx0 + mw > (int)fw) mx0 = (int)fw - mw - 4;
        if (my0 + mh > (int)fh - TASKBAR_H) my0 = (int)fh - TASKBAR_H - mh - 4;

        if (cx >= mx0 && cx <= mx0 + mw && cy >= my0 && cy <= my0 + mh) {
            if (g_desk_menu == 1 && g_desk_menu_idx >= 0 && g_desk_menu_idx < g_desk_item_count) {
                int ih = 32;
                if (cy < my0 + ih) {
                    desk_open_item(g_desk_menu_idx);
                } else {
                    if (g_desk_items[g_desk_menu_idx].is_file) {
                        fs_delete(g_desk_items[g_desk_menu_idx].path);
                    } else {
                        personalization_t* p = get_personalization();
                        p->desktop_icons_mask &= ~(1ULL << g_desk_items[g_desk_menu_idx].global_idx);
                    }
                }
            } else if (g_desk_menu == 2) {
                int ih = 28;
                if (cy < my0 + 4 + ih) {
                    ensure_dir_chain("/home/desktop");
                    int fd = fs_create("/home/desktop/New Document.txt");
                    if (fd >= 0) { fs_write(fd, "New document\n", 13); fs_close(fd); }
                } else if (cy < my0 + 4 + ih * 2) {
                    launch_item(app_name_to_global_idx("Wallpaper"));
                }
            }
            g_desk_menu = 0; g_desk_menu_idx = -1;
            return 1;
        }
        g_desk_menu = 0; g_desk_menu_idx = -1;
    }

    if (cy >= (int)fh - TASKBAR_H) return 0;

    int k = desk_hit(cx, cy, fh);
    if (k < 0) {
        if (clicked) { g_desk_sel = -1; g_desk_menu = 0; }
        return 0;
    }
    if (!clicked) return 1;

    g_desk_sel = k;
    uint32_t now = timer_get_ms();
    if (g_desk_last_click_idx == k && (now - g_desk_last_click_ms) < 400) {
        desk_open_item(k);
        g_desk_last_click_idx = -1;
        g_desk_drag = -1;
    } else {
        g_desk_last_click_idx = k;
        g_desk_last_click_ms = now;
        g_desk_drag = k;
        g_desk_drag_x = cx; g_desk_drag_y = cy;
    }
    return 1;
}

/* Drag tick — desktop icons are fixed to column, so drag just
 * updates selection and we cancel drag on release (no free-floating). */
static void desk_drag_tick(int cx, int cy, int mbtn) {
    (void)cx; (void)cy;
    if (g_desk_drag < 0) return;
    if (!(mbtn & 1)) {
        g_desk_drag = -1;
    }
}

void desk_add_app_icon(int app_idx) {
    personalization_t* p = get_personalization();
    p->desktop_icons_mask |= (1ULL << app_idx);
}

void desk_add_file_icon(const char* path, const char* name) {
    if (!path || !*path) return;
    ensure_dir_chain("/home/desktop");
    const char* fname = name ? name : strrchr(path, '/');
    if (fname && *fname == '/') fname++;
    if (!fname || !*fname) fname = "file.txt";

    char dst[128];
    snprintf(dst, sizeof(dst), "/home/desktop/%s", fname);
    if (!fs_exists(dst) && fs_exists(path)) {
        fs_move(path, dst);
    }
}

void start_gui(void) {
    theme_init();
    gui_running = 1; g_st = 1; search_open = 0; m_idx = 0; wm_init(); gfx_reset_clip();
    gui_load_layout();   /* restore persisted taskbar layout (cfg/layout.cfg) */
    theme_load_cfg();    /* restore persisted theme (cfg/theme.cfg) */
    gui_set_wallpaper_dirty(); /* re-render wallpaper with the loaded theme/settings */
    wm_load_anim_cfg();  /* restore persisted window open-animation (cfg/anim.cfg) */

    while (gui_running) {
        uint32_t loop_now = timer_get_ms();
        int mbtn = mouse_get_buttons(), cx = mouse_get_x(), cy = mouse_get_y();
        uint32_t fw = get_fb_width(), fh = get_fb_height();
        if (fw == 0 || fh == 0) { sleep_ms(10); continue; }

        /* Use the latched press position if a fresh click is still pending:
           it is where the press STARTED (before the click-slide), which is
           where hit-testing must happen. The latch is only consumed here for
           taskbar/menu clicks; when this click belongs to the WM (window),
           it stays pending for wm_tick() below. */
        int use_latched = mouse_has_click();
        if (use_latched) { cx = mouse_get_click_x(); cy = mouse_get_click_y(); }

        int clicked = use_latched || ((mbtn & 1) && !(prev_mouse_btn & 1));


        /* ── Screen saver: idle detection + fullscreen effect ── */
        {
            static uint32_t last_activity = 0;
            static int sa_pmx = -999999, sa_pmy = -999999, sa_pbtn = -1;
            int any_in = (cx != sa_pmx || cy != sa_pmy || mbtn != sa_pbtn ||
                          keyboard_has_input());
            if (any_in) { last_activity = loop_now; sa_pmx = cx; sa_pmy = cy; sa_pbtn = mbtn; }

            if (saver_active) {
                /* grace: ignore input for a short moment after activation so
                   the click/menu that launched the saver can't wake it back
                   immediately ("starts then stops"). */
                int within_grace = (timer_get_ms() - saver_armed_at) < 250;
                if (any_in && !within_grace) {
                    screensaver_deactivate();
                    prev_mouse_btn = mbtn;
                    /* swallow the wake-up click so resuming doesn't
                       accidentally press a button underneath */
                    mouse_consume_click(0, 0);
                }
                anim_frame_begin(timer_get_ms());
                screensaver_frame();
                swap_buffers();
                sleep_task(16);
                continue;
            }

            if (saver_enabled > 0 &&
                (loop_now - last_activity) >= (uint32_t)saver_enabled * 1000u) {
                screensaver_activate();
                last_activity = loop_now;   /* grace so it doesn't wake instantly */
                sa_pmx = cx; sa_pmy = cy; sa_pbtn = mbtn;
                continue;
            }
        }
        int click_handled = 0;

        /* ── Taskbar geometry derived from g_layout (must match draw_taskbar) ── */
        layout_t* L = &g_layout;
        int tb_h = L->size;
        int tb_y = (L->pos == 1) ? 0 : (int)fh - tb_h; /* top or bottom (vertical not handled here) */
        int taskbar_y = tb_y;   /* top edge of the bar (used by hit-rects) */

        int mg2    = 4;
        int start_bw = 36;
        int sep1_x   = mg2 + start_bw + 4;
        int desk_area_x = sep1_x + 6;
        int desk_bw  = 24, desk_bh = tb_h - 8;
        int desk_area_w2 = DESKTOP_COUNT * (desk_bw + 2);
        int sep2_x   = desk_area_x + desk_area_w2 + 4;
        int mid_x    = sep2_x + 6;
        int right_rsv2 = 130 + mg2;
        int snd_icon_x = (int)fw - right_rsv2 + 4;
        int snd_icon_w = 32;

        /* ── Right-click edge detection ── */
        int rclicked = (mbtn & 2) && !(prev_right_btn & 2);

        /* ── Kernel panic screen: owns the whole input + frame ── */
        if (kpanic_active()) {
            kpanic_handle_click(cx, cy, clicked);
            kpanic_render();
            mouse_draw_cursor();
            swap_buffers();
            prev_mouse_btn = mbtn;
            prev_right_btn = mbtn;
            sleep_task(16);
            continue;
        }

        if (clicked || rclicked) {
            /* 0. Desktop App Icons (above the taskbar) — only if no window/overlay covers the spot */
            int win_above = (wm_window_at_point(cx, cy) >= 0);
            int overlay_above = (g_st >= 2 || search_open);
            /* Desktop region = anywhere the taskbar is NOT covering vertically */
            int desktop_ok;
            if (L->pos == 1)      desktop_ok = (cy > tb_h);                 /* top bar */
            else if (L->pos == 0)  desktop_ok = (cy < (int)fh - tb_h);      /* bottom bar */
            else                   desktop_ok = 1;                         /* vertical bars: full height desktop */
            if (!click_handled && !win_above && !overlay_above && desktop_ok && desktop_click(cx, cy, clicked, rclicked)) {
                click_handled = 1;
            }
            /* 1. Start Button Click */
            if (!click_handled && point_in_rect(cx, cy, mg2, taskbar_y + 2, start_bw, tb_h - 4)) {
                gui_toggle_start_menu(); g_st = gui_is_menu_open() ? g_st : 1;
                search_open = 0; click_handled = 1;
            }

            /* 2. Desktop Switcher Clicks */
            if (!click_handled) {
                for (int i = 0; i < DESKTOP_COUNT; i++) {
                    int dx = desk_area_x + i * (desk_bw + 2);
                    int dy = taskbar_y + 4;
                    if (point_in_rect(cx, cy, dx, dy, desk_bw, desk_bh)) {
                        if (i == wm_get_current_desktop()) {
                            int prev = wm_get_previous_desktop();
                            if (prev != i) wm_set_current_desktop(prev);
                        } else wm_set_current_desktop(i);
                        click_handled = 1; break;
                    }
                }
            }

            /* 3. Pinned App Clicks */
            if (!click_handled) {
                personalization_t* p2 = get_personalization();
                uint64_t pm2 = p2->taskbar_pinned_mask;
                int pw2 = 32, cx3 = mid_x;
                for (int i = 0; menu_app_entries[i].name != 0; i++) {
                    int g_idx = menu_app_entries[i].global_idx;
                    if (!(pm2 & (1ULL << g_idx))) continue;
                    if (cx3 + pw2 >= (int)fw - right_rsv2 - 40) break;
                    if (!click_handled && point_in_rect(cx, cy, cx3, taskbar_y + 2, pw2, tb_h - 4)) {
                                int brought = 0;
                                for (int w = 0; w < WM_MAX_WINDOWS; w++) {
                                    wm_window_t* win = wm_get_window_by_index(w);
                                    if (win && strstr(win->title, menu_app_entries[i].name)) {
                                        wm_bring_to_front(win->id); brought = 1; break;
                                    }
                                }
                                if (!brought) launch_item(g_idx);
                            }
                    cx3 += pw2 + 1;
                }
            }

            /* 4. Open Window Tabs Click */
            if (!click_handled) {
                personalization_t* p2b = get_personalization();
                uint64_t pm2b = p2b->taskbar_pinned_mask;
                int ow = 0;
                for (int i = 0; i < WM_MAX_WINDOWS; i++)
                    if (wm_get_window_by_index(i)) ow++;
                int tab_start = mid_x;
                for (int i = 0; menu_app_entries[i].name != 0; i++) {
                    int g_idx = menu_app_entries[i].global_idx;
                    if (!(pm2b & (1ULL << g_idx))) continue;
                    if (tab_start + 32 >= (int)fw - right_rsv2 - 40) break;
                    tab_start += 33;
                }
                tab_start += 8;
                int ta_w = (int)fw - tab_start - right_rsv2;
                if (ta_w < 60) ta_w = 60;
                int tw = 120;
                if (ow > 0) {
                    tw = ta_w / ow - 2;
                    if (tw < 60) tw = 60;
                    if (tw > 150) tw = 150;
                }
                int wx = tab_start;
                int th2 = tb_h - 6;
                int ty2 = taskbar_y + 3;
                for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                    wm_window_t* win = wm_get_window_by_index(i);
                    if (!win) continue;
                    if (wx + tw > (int)fw - right_rsv2) break;
                    if (point_in_rect(cx, cy, wx, ty2, tw, th2)) {
                        if (wm_window_is_minimized(win->id))
                            wm_restore_window(win->id);
                        wm_bring_to_front(win->id);
                        click_handled = 1; break;
                    }
                    wx += tw + 2;
                }
            }

            /* 5. Sound Icon Click */
            if (!click_handled && point_in_rect(cx, cy, snd_icon_x, taskbar_y + 2, snd_icon_w, tb_h - 4)) {
                if (g_st == 22) g_st = 1;
                else g_st = 22;
                click_handled = 1;
            }

            /* 6. Click inside Sound Popover */
            if (!click_handled && g_st == 22) {
                int pw = 60, ph = 260;
                int px = (int)fw - pw - 10;
                int py = (int)fh - tb_h - ph - 6;
                if (py < 4) py = 4;
                if (!point_in_rect(cx, cy, px, py, pw, ph)) {
                    g_st = 1;
                }
                click_handled = 1;
            }

            /* 7. Click inside Start Menu */
            if (!click_handled && g_st >= 2 && g_st <= 19) {
                int mw = 560, mh = 450;
                int mx = 10, my = (int)fh - TASKBAR_H - mh - 10; if (my < 10) my = 10;
                if (point_in_rect(cx, cy, mx, my, mw, mh)) {
                    uint32_t fw = get_fb_width(), fh = get_fb_height();
                    int header_h = 36;
                    int search_h = 26;
                    int search_y = my + header_h + 6;
                    int side_x = mx + 8;
                    int side_y = search_y + search_h + 6;
                    int side_w = 120;
                    int grid_x = side_x + side_w + 12;
                    int grid_y = side_y;
                    int grid_w = mw - (grid_x - mx) - 8;
                    int grid_h = mh - (side_y - my) - 8;

                    // Check Left Category Sidebar
                    for (int c = 0; c < 8; c++) {
                        int cy_c = side_y + c * 34;
                        if (point_in_rect(cx, cy, side_x, cy_c, side_w, 28)) {
                            start_cat_idx = c; menu_scroll = 0; click_handled = 1; break;
                        }
                    }

                    // Scroll arrows
                    if (!click_handled) {
                        int card_w = 118, card_h = 60, cols = 3, gap = 6;
                        int total_vis = 0;
                        for (int i = 0; menu_app_entries[i].name != 0; i++) {
                            if (start_search_len > 0) {
                                const char* h = menu_app_entries[i].name;
                                const char* n = start_search_buf;
                                int match = 1;
                                while (*n) { char a = *h, b = *n; if (a >= 'A' && a <= 'Z') a += 32; if (b >= 'A' && b <= 'Z') b += 32; if (a != b) { match = 0; break; } h++; n++; }
                                if (!match) continue;
                            } else {
                                if (start_cat_idx != 0 && menu_app_entries[i].category != start_cat_idx) continue;
                            }
                            total_vis++;
                        }
                        int has_sc = (total_vis * (card_h + gap) > grid_h + 24);
                        int scroll_h2 = has_sc ? 16 : 0;
                        int rows_vis = (grid_h - scroll_h2) / (card_h + gap);
                        int max_vis = rows_vis * cols;
                        int arr_y = grid_y + grid_h - 14;
                        if (menu_scroll > 0 && point_in_rect(cx, cy, grid_x, arr_y, 20, 12)) {
                            menu_scroll = menu_scroll - rows_vis; if (menu_scroll < 0) menu_scroll = 0;
                            click_handled = 1;
                        }
                        if (menu_scroll < total_vis - max_vis && point_in_rect(cx, cy, grid_x + grid_w - 20, arr_y, 20, 12)) {
                            menu_scroll = menu_scroll + rows_vis; click_handled = 1;
                        }
                    }

                     // Check Right Content App Cards Grid
                    if (!click_handled) {
                        int card_w = 118, card_h = 60, cols = 3, gap = 6;
                        int v_idx = 0;
                        for (int i = 0; menu_app_entries[i].name != 0; i++) {
                            if (start_search_len > 0) {
                                const char* h = menu_app_entries[i].name;
                                const char* n = start_search_buf;
                                int match = 1;
                                while (*n) { char a = *h, b = *n; if (a >= 'A' && a <= 'Z') a += 32; if (b >= 'A' && b <= 'Z') b += 32; if (a != b) { match = 0; break; } h++; n++; }
                                if (!match) continue;
                            } else {
                                if (start_cat_idx != 0 && menu_app_entries[i].category != start_cat_idx) continue;
                            }
                            if (v_idx < menu_scroll) { v_idx++; continue; }
                            int idx = v_idx - menu_scroll;
                            int r = idx / cols, col = idx % cols;
                            int acx = grid_x + col * (card_w + gap);
                            int acy = grid_y + r * (card_h + gap);
                            if (point_in_rect(cx, cy, acx, acy, card_w, card_h)) {
                                launch_item(menu_app_entries[i].global_idx);
                                click_handled = 1; break;
                            }
                            v_idx++;
                        }
                    }
                } else {
                    g_st = 1;
                }
            }

            /* 8. Click inside Taskbar Pins Selector Modal */
            if (!click_handled && g_st == 21) {
                int mw = 520, mh = 420;
                int mx = (int)(fw - mw) / 2, my = (int)(fh - mh) / 2 - 20; if (my < 10) my = 10;
                if (point_in_rect(cx, cy, mx, my, mw, mh)) {
                    if (point_in_rect(cx, cy, mx + mw - 32, my + 12, 22, 22)) {
                        g_st = 1; click_handled = 1;
                    }
                    int btn_w = 120, btn_h = 32;
                    int btn_x = mx + (mw - btn_w) / 2, btn_y = my + mh - 42;
                    if (!click_handled && point_in_rect(cx, cy, btn_x, btn_y, btn_w, btn_h)) {
                        g_st = 1; click_handled = 1;
                    }
                    // App cards: iterate all menu_app_entries
                    if (!click_handled) {
                        personalization_t* p = get_personalization();
                        int app_count = 0;
                        while (menu_app_entries[app_count].name != 0) app_count++;
                        int card_w = 216, card_h = 46, cols = 2;
                        int rows_per_page = (mh - 100) / (card_h + 8);
                        static int pp = 0;
                        int start_idx = pp * cols * rows_per_page;
                        int end_idx = start_idx + cols * rows_per_page;
                        if (end_idx > app_count) end_idx = app_count;
                        // Arrow clicks for pagination
                        int arr_y = my + mh - 40;
                        if (point_in_rect(cx, cy, mx + 12, arr_y, 20, 20)) { if (pp > 0) pp--; click_handled = 1; }
                        if (point_in_rect(cx, cy, mx + mw - 32, arr_y, 20, 20)) {
                            int tp = (app_count + cols * rows_per_page - 1) / (cols * rows_per_page);
                            if (pp < tp - 1) pp++; click_handled = 1;
                        }
                        for (int i = start_idx; i < end_idx && !click_handled; i++) {
                            int idx = i - start_idx;
                            int col = idx % cols, row = idx / cols;
                            int acx = mx + 16 + col * (card_w + 16);
                            int acy = my + 60 + row * (card_h + 8);
                            if (point_in_rect(cx, cy, acx, acy, card_w, card_h)) {
                                p->taskbar_pinned_mask ^= (1ULL << menu_app_entries[i].global_idx);
                                click_handled = 1;
                            }
                        }
                    }
                    click_handled = 1;
                } else {
                    g_st = 1;
                }
            }

            /* 9. Spotlight Search Overlay Click */
            if (!click_handled && search_open) {
                int mw = 600, mh = 400;
                int mx = (fw - mw) / 2, my = (fh - mh) / 2 - 40;
                if (point_in_rect(cx, cy, mx, my, mw, mh)) {
                    int ry = my + 80;
                    int rel_y = cy - ry;
                    if (rel_y >= 0) { 
                        int idx = rel_y / 48; 
                        int limit = (search_result_count < 6) ? search_result_count : 6;
                        if (idx < limit && (idx + search_scroll) < search_result_count) {
                            launch_search_result(search_results[idx + search_scroll]); 
                        }
                    }
                } else search_open = 0;
                click_handled = 1;
            }

            /* 8b. Click inside Taskbar Layout Modal (g_st == 23) */
            if (!click_handled && g_st == 23) {
                int mw = 600, mh = 460;
                int mx = (int)(fw - mw) / 2, my = (int)(fh - mh) / 2 - 20; if (my < 10) my = 10;
                if (point_in_rect(cx, cy, mx + mw - 32, my + 12, 22, 22)) {
                    g_st = 1; click_handled = 1;
                } else if (point_in_rect(cx, cy, mx, my, mw, mh)) {
                    click_handled = 1; /* consumed; internals handled by renderer */
                }
            }

            if (!click_handled && (g_st >= 2 || search_open)) {
                /* Debounce: the click that opened the menu (or search) is
                   still latched on the very next frames, and during the open
                   animation the menu hasn't slid to its final position yet,
                   so the outside-click close would fire immediately and
                   "close" a menu we just opened. Ignore it for MENU_ANIM_MS
                   after an open so the opener click is consumed first. */
                if ((int)(timer_get_ms() - last_menu_open_ms) < MENU_ANIM_MS) {
                    click_handled = 1; /* swallow — it's the open click */
                } else {
                    g_st = 1; search_open = 0; click_handled = 1;
                }
            }
        }

        /* The GUI handled this click (taskbar/menu/modal) — consume the
           latch so wm_tick() does not double-process it. If the GUI did not
           handle it, the click belongs to a window and wm_tick() will
           consume it. */
        if (clicked && click_handled) {
            mouse_consume_click(0, 0);
        }

        draw_premium_wallpaper(); 
        desktop_tick();          /* desktop app icons (on the wallpaper, under windows) */
        desk_drag_tick(cx, cy, mbtn);  /* advance icon drag + grid snap */
        wm_tick();
        wm_user_cleanup_dead();  /* close user windows whose ring-3 task died */


        gui_sample_sysmon();   /* refresh CPU/RAM for menu (still tracked) */
        draw_taskbar_mac();
        if (g_st >= 2 && g_st <= 19) {
            render_menu();
            menu_anim_finalize_close();   /* complete a deferred close animation */
        }
        if (g_st == 21) render_taskbar_pins_modal();
        if (g_st == 22) render_sound_popover();
        if (g_st == 23) render_taskbar_layout_modal();
        if (search_open) {
            int wd = mouse_get_wheel_delta();
            if (wd != 0) {
                mouse_clear_wheel_delta();
                search_scroll -= wd;
                int max_scr = search_result_count - 6;
                if (search_scroll < 0) search_scroll = 0;
                if (search_scroll > max_scr) search_scroll = (max_scr > 0 ? max_scr : 0);
            }
            render_search_panel();
        }

        /* Wheel scroll for the Start Menu app grid (menu_scroll). Only the
           focused overlay consumes the wheel per frame; wm_tick() already
           handled window scrolling when no overlay was open. */
        if (g_st >= 2 && g_st <= 19 && !search_open) {
            int wd = mouse_get_wheel_delta();
            if (wd != 0) {
                mouse_clear_wheel_delta();
                int card_h = 60, gap = 6, cols = 3;
                int mw = 560, mh = 450;
                int menu_y = (int)fh - TASKBAR_H - mh - 10; if (menu_y < 10) menu_y = 10;
                int header_h = 36, search_h = 26;
                int side_y = menu_y + header_h + 6 + search_h + 6;
                int grid_h = mh - (side_y - menu_y) - 8;
                int scroll_h2 = 16;
                int rows_vis = (grid_h - scroll_h2) / (card_h + gap);
                if (rows_vis < 1) rows_vis = 1;
                menu_scroll += (wd > 0 ? -rows_vis : rows_vis);
                if (menu_scroll < 0) menu_scroll = 0;
            }
        }

        mouse_draw_cursor();
        kerror_render();          /* non-fatal error toast (if any) */
        swap_buffers();
        prev_mouse_btn = mbtn;
        prev_right_btn = mbtn;
        sleep_task(16);
    }
}

void desk_remove_icon_by_path(const char* path) { (void)path; }
