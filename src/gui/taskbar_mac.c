/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/
/*
 * EigenOS slim top taskbar + Haiku-style cascading start menu (ring 0).
 *
 * Bar (polybar-inspired, ultra slim):
 *   [lambda] 1 2 3 4 5 | [window buttons] ...  CPU 42%  RAM 61%  21:37  [pwr]
 * Flat surfaces, hairline border, quiet grey modules, one accent.
 *
 * Start menu (Haiku Deskbar inspired):
 *   lambda button -> dropdown with a small lambda banner, app categories,
 *   and fly-out submenus listing the apps in each category. The button
 *   glyph can be replaced by any imported .bmp (right-click the button,
 *   or drop files into /home/user/icons).
 *
 * The searchable grid launcher remains available via Super+S.
 * All text renders through the kernel FreeType engine (DejaVuSans,
 * anti-aliased) with an 8x16 fallback so the shell still works.
 */

#include "gui/gui.h"
#include "gui/wm.h"
#include "gui/ftfont.h"
#include "gui/app_icons.h"
#include "gui/wallpaper_mgr.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard.h"
#include "drivers/time/rtc.h"
#include "filesystem/filesystem.h"
#include "libs/bmp.h"
#include "kernel/acpi.h"
#include "kernel/time/timer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── slim bar metrics ──────────────────────────────────────── */
#define TB_DESKTOPS      5
#define TB_MIN_H         16
#define TB_DESK_SEG_W    13     /* per-workspace digit cell          */
#define TB_WIN_MAX_W     150
#define TB_TRAY_GAP      8
#define TB_TEXT_PX       12     /* bar font size                     */
#define TB_SMALL_PX      10

/* ── cascade menu metrics ──────────────────────────────────── */
#define CM_X             6
#define CM_WIDTH         204
#define CM_BANNER_H      40
#define CM_ITEM_H        25
#define CM_PAD           6
#define SM_WIDTH         200
#define SM_ITEM_H        25
#define SM_MAX_ROWS      11

/* ── grid launcher (Super+S search) metrics ────────────────── */
#define LAUNCHER_MARGIN  8
#define LAUNCHER_HEAD_H  54
#define LAUNCHER_SIDE_W  148
#define LAUNCHER_FOOT_H  30
#define CARD_W           150
#define CARD_H           64
#define CARD_GAP         8

/* ── imported start-icon metrics ───────────────────────────── */
#define SICON_PX         32     /* cached thumbnail edge             */
#define ICON_DIR         "/home/user/icons"
#define ICON_MAX         9      /* picker rows (incl. Default)       */
#define ICON_CFG         "cfg/starticon.cfg"

static const char* tb_cat_names[8] = {
    "All Applications", "Productivity", "System", "Games",
    "Graphics", "Debug", "Accessibility", "Networking"
};

/* ── state ─────────────────────────────────────────────────── */
static int g_launcher   = 0;   /* searchable grid launcher open    */
static int g_pwpop      = 0;   /* power popover open               */
static int g_cat        = 0;   /* grid: selected category          */
static int g_scroll     = 0;   /* grid: card scroll (rows)         */
static int g_sel        = -1;  /* grid: keyboard selection         */
static char g_q[64];
static int  g_qlen      = 0;
static int  g_prev_mid  = 0;
static uint32_t g_open_ms  = 0;
static uint32_t g_caret_ms = 0;

static int g_filtered[128];
static int g_nfiltered  = 0;

/* cascade start menu */
static int g_menu     = 0;     /* main dropdown open               */
static int g_menu_sub = -1;    /* open submenu category (0..7)     */
static int g_ms_main  = -1;    /* keyboard selection, main row     */
static int g_ms_sub   = -1;    /* keyboard selection, submenu row  */

/* start-button icon picker */
static int g_iconpick = 0;
static int g_ipsel    = 0;

/* context menu state (declared early: close() touches it) */
static int  g_ctx = 0, g_ctx_x = 0, g_ctx_y = 0, g_ctx_rows = 0;
static int  g_ctx_win = -1;
static char g_ctx_app[40];

static const char* taskbar_icon_for_window(const char* title);

void gui_system_shutdown(void);
extern void acpi_reboot(void);

/* ── text helpers (FreeType with 8x16 fallback) ────────────── */
static int tlh(int px) {
    return ftfont_ready() ? ftfont_height(px) : 16;
}
static void ttext(int x, int y, const char* s, uint32_t c, int px) {
    if (ftfont_ready()) ftfont_draw(x, y, s, c, px);
    else gfx_draw_string_transparent(x, y, s, c);
}
static int twidth(const char* s, int px) {
    if (ftfont_ready()) return ftfont_width(s, px);
    int n = 0; while (s[n]) n++;
    return n * 8;
}
static void ttext_trunc(int x, int y, int max_w, const char* s, uint32_t c, int px) {
    if (ftfont_ready()) ftfont_draw_trunc(x, y, max_w, s, c, px);
    else gfx_draw_string_transparent(x, y, s, c);
}

static int pir(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

/* ════════════════════════════════════════════════════════════
 * Imported start-button icon (.bmp, decode-once 32x32 cache)
 * ════════════════════════════════════════════════════════════ */
static char    g_sicon_path[160];
static uint8_t g_sicon_px[SICON_PX * SICON_PX * 3];
static int     g_sicon_ok   = 0;
static int     g_sicon_done = 0;

static char    g_icon_names[ICON_MAX][48];   /* candidate .bmp files */
static int     g_icon_n = 0;

static void sicon_load(void) {
    g_sicon_done = 1;
    g_sicon_ok   = 0;
    g_sicon_path[0] = 0;

    int fd = fs_open(ICON_CFG, 0);
    if (fd >= 0) {
        char b[160];
        int n = fs_read(fd, b, (int)sizeof(b) - 1);
        fs_close(fd);
        if (n > 0) {
            b[n] = 0;
            int e = 0;
            while (b[e] && b[e] != '\n' && b[e] != '\r' && b[e] != ' ') e++;
            b[e] = 0;
            if (e > 0 && e < (int)sizeof(g_sicon_path)) {
                memcpy(g_sicon_path, b, (size_t)e + 1);
            }
        }
    }
    if (!g_sicon_path[0]) return;

    bmp_image_t img; img.pixels = 0;
    if (!wallpaper_mgr_decode_file(g_sicon_path, &img)) return;
    if (img.width <= 0 || img.height <= 0 || !img.pixels) { bmp_free(&img); return; }

    /* center-crop a square region, nearest-neighbor down to 32x32 */
    int iw = img.width, ih = img.height;
    int side = iw < ih ? iw : ih;
    int sx0 = (iw - side) / 2, sy0 = (ih - side) / 2;
    for (int dy = 0; dy < SICON_PX; dy++) {
        int sy = sy0 + dy * side / SICON_PX;
        const uint8_t* row = img.pixels + ((long)sy * iw) * 3;
        uint8_t* dst = g_sicon_px + dy * SICON_PX * 3;
        for (int dx = 0; dx < SICON_PX; dx++) {
            int sx = sx0 + dx * side / SICON_PX;
            const uint8_t* p = row + sx * 3;
            dst[dx * 3] = p[0]; dst[dx * 3 + 1] = p[1]; dst[dx * 3 + 2] = p[2];
        }
    }
    bmp_free(&img);
    g_sicon_ok = 1;
}

static void sicon_ensure(void) { if (!g_sicon_done) sicon_load(); }

static void sicon_apply(const char* path) {
    fs_mkdir("cfg");
    fs_delete(ICON_CFG);
    if (path && path[0]) {
        int fd = fs_create(ICON_CFG);
        if (fd >= 0) {
            char b[160];
            int n = snprintf(b, sizeof(b), "%s\n", path);
            fs_write(fd, b, n);
            fs_close(fd);
        }
    }
    g_sicon_done = 0;          /* reload lazily on next draw */
    g_sicon_ok   = 0;
}

/* scan ICON_DIR for .bmp candidates (picker entries) */
static int dir_find_any(const char* name) {
    for (int i = 0; i < 256; i++) {
        char n[64]; int s = 0, t = 0, par = -1; uint8_t fl = 0; uint32_t mt = 0;
        if (fs_get_node(i, n, &s, &t, &par, &fl, &mt) == 0 && t == 1 &&
            strcmp(n, name) == 0) return i;
    }
    return -1;
}

static void icons_scan(void) {
    g_icon_n = 0;
    ensure_dir_chain(ICON_DIR);
    int home  = dir_find_any("home");
    int user  = -1;
    int icons = -1;
    if (home >= 0) {
        for (int i = 0; i < 256 && user < 0; i++) {
            char n[64]; int s = 0, t = 0, par = -1; uint8_t fl = 0; uint32_t mt = 0;
            if (fs_get_node(i, n, &s, &t, &par, &fl, &mt) == 0 && t == 1 &&
                strcmp(n, "user") == 0 && par == home) user = i;
        }
    }
    if (user >= 0) {
        for (int i = 0; i < 256; i++) {
            char n[64]; int s = 0, t = 0, par = -1; uint8_t fl = 0; uint32_t mt = 0;
            if (fs_get_node(i, n, &s, &t, &par, &fl, &mt) == 0 && t == 1 &&
                strcmp(n, "icons") == 0 && par == user) { icons = i; break; }
        }
    }
    if (icons < 0) icons = dir_find_any("icons");   /* fallback: name-only */
    if (icons < 0) return;

    for (int i = 0; i < 256 && g_icon_n < ICON_MAX - 1; i++) {
        char n[64]; int s = 0, t = 0, par = -1; uint8_t fl = 0; uint32_t mt = 0;
        if (fs_get_node(i, n, &s, &t, &par, &fl, &mt) != 0 || t != 0 || par != icons)
            continue;
        int ln = 0; while (n[ln]) ln++;
        if (ln < 4 || ln > 44) continue;
        const char* ext = n + ln - 4;
        if (strcmp(ext, ".bmp") != 0 && strcmp(ext, ".BMP") != 0) continue;
        memcpy(g_icon_names[g_icon_n], n, (size_t)ln + 1);
        g_icon_n++;
    }
}

/* ════════════════════════════════════════════════════════════
 * Geometry
 * ════════════════════════════════════════════════════════════ */

/* right-side tray modules, formatted once per frame so drawing and
   hit-testing share identical widths */
typedef struct {
    int bar_h;
    int menu_x, menu_w;
    int desk_x, desk_w;
    int win_x, win_w;
    int cpu_x, cpu_w;
    int ram_x, ram_w;
    int clk_x, clk_w;
    int pwr_x, pwr_w;
    int tray_y, tray_h;
} tb_geom_t;

static void tray_strings(char cpu[16], char ram[16], char clk[20]) {
    snprintf(cpu, 16, "CPU %d%%", g_sysmon_cpu);
    snprintf(ram, 16, "RAM %d%%", g_sysmon_ram);
    time_t now;
    get_time(&now);
    personalization_t* P = get_personalization();
    if (P->clock_24h) snprintf(clk, 20, "%02d:%02d", now.hour, now.minute);
    else {
        int h12 = now.hour % 12; if (h12 == 0) h12 = 12;
        snprintf(clk, 20, "%d:%02d %s", h12, now.minute, now.hour < 12 ? "AM" : "PM");
    }
}

static void tb_geom(tb_geom_t* g) {
    uint32_t fw = get_fb_width();
    layout_t* L = gui_get_layout();
    int bh = L->size;
    if (bh < TB_MIN_H) bh = TB_MIN_H;
    g->bar_h = bh;

    g->tray_y = 2;
    g->tray_h = bh - 4;

    g->menu_w = bh;                       /* square lambda button */
    g->menu_x = 5;

    g->desk_x = g->menu_x + g->menu_w + 8;
    g->desk_w = TB_DESKTOPS * TB_DESK_SEG_W;

    /* tray widths from live strings */
    char cpu[16], ram[16], clk[20];
    tray_strings(cpu, ram, clk);
    int gap = TB_TRAY_GAP;
    g->cpu_w = L->monitor   ? twidth(cpu, TB_TEXT_PX) + 20 : 0;
    g->ram_w = L->monitor   ? twidth(ram, TB_TEXT_PX) + 20 : 0;
    g->clk_w = L->show_clock ? twidth(clk, TB_TEXT_PX) + 22 : 0;
    g->pwr_w = bh;

    int right = (int)fw - 6;
    g->pwr_x = right - g->pwr_w;
    g->clk_x = g->pwr_x - gap - g->clk_w;
    g->ram_x = g->clk_x - gap - g->ram_w;
    g->cpu_x = g->ram_x - gap - g->cpu_w;
    if (!L->monitor)   { g->cpu_x = g->ram_x; }
    if (!L->show_clock) { g->clk_x = g->ram_x; }

    g->win_x = g->desk_x + g->desk_w + 10;
    g->win_w = g->cpu_x - gap - 2 - g->win_x;
    if (g->win_w < 40) g->win_w = 40;
}

/* ── grid launcher geometry (unchanged layout, slim-friendly) ── */
typedef struct {
    int x, y, w, h;
    int side_x, side_y, side_w;
    int grid_x, grid_y, grid_w, grid_h;
    int cols;
    int foot_y;
} lc_geom_t;

static void lc_geom(lc_geom_t* g) {
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    tb_geom_t t; tb_geom(&t);
    g->w = (int)fw - 2 * LAUNCHER_MARGIN;
    if (g->w > 760) g->w = 760;
    g->h = (int)fh - t.bar_h - LAUNCHER_MARGIN - 12;
    if (g->h > 500) g->h = 500;
    g->x = LAUNCHER_MARGIN;
    g->y = t.bar_h + 6;
    g->side_x = g->x + 10;
    g->side_y = g->y + LAUNCHER_HEAD_H + 6;
    g->side_w = LAUNCHER_SIDE_W;
    g->grid_x = g->side_x + g->side_w + 10;
    g->grid_y = g->side_y;
    g->grid_w = g->x + g->w - g->grid_x - 14;
    g->foot_y = g->y + g->h - LAUNCHER_FOOT_H;
    g->grid_h = g->foot_y - 6 - g->grid_y;
    g->cols = (g->grid_w + CARD_GAP) / (CARD_W + CARD_GAP);
    if (g->cols < 1) g->cols = 1;
}

/* ── cascade menu geometry ─────────────────────────────────── */
typedef struct {
    int x, y, w, h;
    int items_n;
    int cats[8];                /* category id per row           */
} cm_geom_t;

static void cat_counts(int out[8]) {
    for (int c = 0; c < 8; c++) out[c] = 0;
    for (int i = 0; menu_app_entries[i].name != 0; i++) {
        out[0]++;
        int c = menu_app_entries[i].category;
        if (c >= 1 && c <= 7) out[c]++;
    }
}

static int cm_build(cm_geom_t* g) {
    int counts[8];
    cat_counts(counts);
    int n = 0;
    g->cats[n++] = 0;                          /* All Applications */
    for (int c = 1; c <= 7 && n < 8; c++)
        if (counts[c] > 0) g->cats[n++] = c;
    tb_geom_t t; tb_geom(&t);
    uint32_t fh = get_fb_height();
    g->x = CM_X;
    g->y = t.bar_h + 4;
    g->w = CM_WIDTH;
    g->items_n = n;
    g->h = CM_BANNER_H + CM_PAD * 2 + n * CM_ITEM_H;
    if (g->y + g->h > (int)fh - 8) g->h = (int)fh - 8 - g->y;
    return n;
}

static void cm_item_rect(const cm_geom_t* g, int i, int* rx, int* ry, int* rw, int* rh) {
    *rx = g->x + CM_PAD;
    *ry = g->y + CM_BANNER_H + CM_PAD + i * CM_ITEM_H;
    *rw = g->w - CM_PAD * 2;
    *rh = CM_ITEM_H - 2;
}

/* apps of one category (cat 0 = all) */
static int sm_apps(int cat, int* idxs, int max) {
    int n = 0;
    for (int i = 0; menu_app_entries[i].name != 0 && n < max; i++) {
        if (cat != 0 && menu_app_entries[i].category != cat) continue;
        idxs[n++] = i;
    }
    return n;
}

typedef struct {
    int x, y, w, h, n;
    int items[64];
} sm_geom_t;

static int sm_build(int cat, const cm_geom_t* mg, int anchor_row, sm_geom_t* sg) {
    sg->n = sm_apps(cat, sg->items, 64);
    if (sg->n > SM_MAX_ROWS) sg->n = SM_MAX_ROWS;
    sg->w = SM_WIDTH;
    sg->h = CM_PAD * 2 + sg->n * SM_ITEM_H;
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    sg->x = mg->x + mg->w + 4;
    if (sg->x + sg->w > (int)fw - 6) sg->x = mg->x - sg->w - 4;
    int rx, ry, rw, rh;
    cm_item_rect(mg, anchor_row, &rx, &ry, &rw, &rh);
    sg->y = ry - 2;
    if (sg->y + sg->h > (int)fh - 6) sg->y = (int)fh - 6 - sg->h;
    if (sg->y < mg->y) sg->y = mg->y;
    return sg->n;
}

static void sm_item_rect(const sm_geom_t* g, int j, int* rx, int* ry, int* rw, int* rh) {
    *rx = g->x + CM_PAD;
    *ry = g->y + CM_PAD + j * SM_ITEM_H;
    *rw = g->w - CM_PAD * 2;
    *rh = SM_ITEM_H - 2;
}

/* ── filtering (grid search) ───────────────────────────────── */
static int ci_sub(const char* hay, const char* ned) {
    while (*hay) {
        const char* h = hay;
        const char* n = ned;
        while (*n) {
            char a = *h, b = *n;
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b || !a) break;
            h++; n++;
        }
        if (!*n) return 1;
        hay++;
    }
    return 0;
}

static void refilter(void) {
    g_nfiltered = 0;
    for (int i = 0; menu_app_entries[i].name != 0 && g_nfiltered < 128; i++) {
        if (g_cat != 0 && menu_app_entries[i].category != g_cat) continue;
        if (g_qlen > 0 && !ci_sub(menu_app_entries[i].name, g_q)) continue;
        g_filtered[g_nfiltered++] = i;
    }
}

/* ── public state API ──────────────────────────────────────── */
int taskbar_overlay_open(void) {
    return g_launcher || g_pwpop || g_menu || g_iconpick;
}

void taskbar_launcher_close(void) {
    g_launcher = 0; g_pwpop = 0;
    g_menu = 0; g_menu_sub = -1; g_ms_main = -1; g_ms_sub = -1;
    g_iconpick = 0; g_ctx = 0;
}

void taskbar_menu_close(void) {
    taskbar_launcher_close();
}

void taskbar_menu_toggle(void) {
    if (g_menu) { taskbar_launcher_close(); return; }
    taskbar_launcher_close();
    g_menu = 1;
    g_menu_sub = -1; g_ms_main = -1; g_ms_sub = -1;
    g_open_ms = timer_get_ms();
}

void taskbar_launcher_toggle(void) {
    if (g_launcher) { taskbar_launcher_close(); return; }
    taskbar_launcher_close();
    g_launcher = 1;
    g_q[0] = 0; g_qlen = 0; g_cat = 0; g_scroll = 0; g_sel = -1;
    refilter();
    g_open_ms = timer_get_ms();
}

void taskbar_launcher_open_search(void) {
    taskbar_launcher_close();
    g_launcher = 1;
    g_cat = 0; g_scroll = 0; g_sel = -1;
    g_q[0] = 0; g_qlen = 0;
    refilter();
    g_open_ms = timer_get_ms();
}

static void launch_grid_item(int i) {
    if (i < 0 || i >= g_nfiltered) return;
    launch_item(menu_app_entries[g_filtered[i]].global_idx);
    taskbar_launcher_close();
}

static void launch_menu_app(int entry_idx) {
    if (entry_idx < 0) return;
    launch_item(menu_app_entries[entry_idx].global_idx);
    taskbar_launcher_close();
}

/* ── pinned apps + context menu ─────────────────────────── */
static int app_pinned(int gi) {
    return (int)((get_personalization()->taskbar_pinned_mask >> gi) & 1ULL);
}
static void set_pin(int gi, int on) {
    personalization_t* p = get_personalization();
    if (on) p->taskbar_pinned_mask |=  (1ULL << gi);
    else    p->taskbar_pinned_mask &= ~(1ULL << gi);
}

static const app_item_t* win_owner(const wm_window_t* w) {
    const char* m = taskbar_icon_for_window(w ? w->title : 0);
    if (!m) return 0;
    for (int i = 0; menu_app_entries[i].name; i++)
        if (strcmp(m, menu_app_entries[i].name) == 0)
            return &menu_app_entries[i];
    return 0;
}

typedef struct {
    int               is_static;
    const app_item_t* app;
    wm_window_t*      win;
} tslot_t;

static int tb_slots(const tb_geom_t* t, tslot_t* out, int max,
                    int* xs, int* ws) {
    wm_window_t* wins[WM_MAX_WINDOWS]; int nw = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t* w = wm_get_window_by_index(i);
        if (w) wins[nw++] = w;
    }
    int used[WM_MAX_WINDOWS]; memset(used, 0, sizeof(used));
    int n = 0;
    personalization_t* P = get_personalization();
    for (int i = 0; menu_app_entries[i].name && n < max; i++) {
        if (!((P->taskbar_pinned_mask >> menu_app_entries[i].global_idx) & 1ULL))
            continue;
        int attached = -1;
        for (int j = 0; j < nw; j++) {
            if (used[j]) continue;
            const app_item_t* o = win_owner(wins[j]);
            if (o && strcmp(o->name, menu_app_entries[i].name) == 0) { attached = j; break; }
        }
        if (attached >= 0) {
            used[attached] = 1;
            out[n].is_static = 0; out[n].app = &menu_app_entries[i];
            out[n].win = wins[attached]; n++;
            for (int j = attached + 1; j < nw && n < max; j++) {
                if (used[j]) continue;
                const app_item_t* o2 = win_owner(wins[j]);
                if (o2 && strcmp(o2->name, menu_app_entries[i].name) == 0) {
                    used[j] = 1;
                    out[n].is_static = 0; out[n].app = &menu_app_entries[i];
                    out[n].win = wins[j]; n++;
                }
            }
        } else {
            out[n].is_static = 1; out[n].app = &menu_app_entries[i];
            out[n].win = 0; n++;
        }
    }
    for (int j = 0; j < nw && n < max; j++) {
        if (used[j]) continue;
        out[n].is_static = 0; out[n].app = win_owner(wins[j]);
        out[n].win = wins[j]; n++;
    }
    if (n == 0 || t->win_w <= 40) return 0;
    int gap = 4;
    int bw = (t->win_w - (n - 1) * gap) / n;
    if (bw > TB_WIN_MAX_W) bw = TB_WIN_MAX_W;
    if (bw < 28) bw = 28;
    int wx = t->win_x;
    for (int i = 0; i < n; i++) { xs[i] = wx; ws[i] = bw; wx += bw + gap; }
    return n;
}

static void ctx_open(int mx, int my, const app_item_t* app, wm_window_t* win) {
    g_ctx = 1; g_ctx_x = mx; g_ctx_y = my; g_ctx_rows = 0;
    g_ctx_win = win ? (int)win->id : -1;
    strncpy(g_ctx_app, app ? app->name : "", sizeof(g_ctx_app) - 1);
    g_ctx_app[sizeof(g_ctx_app) - 1] = 0;
}

/* ═══════════════════════════════ RENDER ═════════════════════ */

/* EigenOS lambda mark from two strokes, parametric size */
static void draw_lambda_mark(int cx, int cy, int e, uint32_t ca, uint32_t cb) {
    int k = e / 6;                       /* knee offset from center   */
    gfx_draw_line(cx - e, cy - e, cx + k, cy + e, ca);
    gfx_draw_line(cx - e + 1, cy - e, cx + k + 1, cy + e, ca);
    gfx_draw_line(cx + k, cy + e, cx + e + k / 2, cy - e, cb);
    gfx_draw_line(cx + k + 1, cy + e, cx + e + k / 2 + 1, cy - e, ca);
}

static void draw_start_glyph(int cx, int cy, int e) {
    sicon_ensure();
    if (g_sicon_ok) {
        int s = e * 2 + 2;
        gfx_draw_rgb_bitmap_scaled(cx - s / 2, cy - s / 2, s, s,
                                   g_sicon_px, SICON_PX, SICON_PX);
    } else {
        uint32_t acc = get_accent_color();
        draw_lambda_mark(cx, cy, e, acc, gfx_lighten(acc, 45));
    }
}

/* map a window title to a themed icon name (exact hit, then registry prefix) */
extern int icon_theme_draw(const char* app, int x, int y, int size);
static const char* taskbar_icon_for_window(const char* title) {
    if (!title || !title[0]) return 0;
    for (int i = 0; menu_app_entries[i].name; i++)
        if (strcmp(title, menu_app_entries[i].name) == 0) return title;
    for (int i = 0; menu_app_entries[i].name; i++) {
        int n = 0; const char* a = menu_app_entries[i].name;
        while (a[n] && title[n] && a[n] == title[n]) n++;
        if (!a[n]) return a;
    }
    return 0;
}

static void draw_window_buttons(const tb_geom_t* t) {
    uint32_t txt  = theme_get_color(THEME_ROLE_TASKBAR_TEXT);
    uint32_t mut  = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t hov  = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t selc = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    int mx = mouse_get_x(), my = mouse_get_y();

    tslot_t sl[WM_MAX_WINDOWS + 16];
    int xs[WM_MAX_WINDOWS + 16], ws_[WM_MAX_WINDOWS + 16];
    int count = tb_slots(t, sl, WM_MAX_WINDOWS + 16, xs, ws_);
    if (count == 0) return;

    int focused = wm_get_focused();
    for (int i = 0; i < count; i++) {
        int wx = xs[i], bw = ws_[i];
        wm_window_t* win = sl[i].win;
        int is_static = sl[i].is_static;
        int is_focus  = win && (win->id == focused);
        int hovr      = pir(mx, my, wx, 0, bw, t->bar_h);

        if (is_focus)  gfx_fill_rect(wx, 1, bw, t->bar_h - 2, selc);
        else if (hovr) gfx_fill_rect_alpha(wx, 1, bw, t->bar_h - 2, hov, 120);

        const char* disp = is_static ? sl[i].app->name : win->title;
        int is_min = win && (win->flags & WM_FLAG_MINIMIZED) != 0;

        int isz = t->bar_h - 8;
        const char* iname = (!is_static && is_min) ? 0 : sl[i].app->name;
        int have_icon = iname && icon_theme_draw(iname, wx + 5, 4, isz);
        if (!have_icon && !is_static) {
            uint32_t dc = is_min ? mut : win->accent_color;
            gfx_fill_circle(wx + 8, t->bar_h / 2, 2, dc);
        }

        int tx0 = wx + (have_icon ? isz + 9 : 14);
        int ty = (t->bar_h - tlh(TB_TEXT_PX)) / 2 + 1;
        ttext_trunc(tx0, ty, wx + bw - tx0 - 4, disp,
                    is_focus ? txt : mut, TB_TEXT_PX);
    }
}

static void draw_tray(const tb_geom_t* t) {
    uint32_t txt = theme_get_color(THEME_ROLE_TASKBAR_TEXT);
    uint32_t mut = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t acc = get_accent_color();
    int mx = mouse_get_x(), my = mouse_get_y();
    layout_t* L = gui_get_layout();

    char cpu[16], ram[16], clk[20];
    tray_strings(cpu, ram, clk);

    /* CPU module */
    if (L->monitor && t->cpu_w > 0) {
        int hovr = pir(mx, my, t->cpu_x, 0, t->cpu_w, t->bar_h);
        if (hovr) gfx_fill_rect_alpha(t->cpu_x, 1, t->cpu_w, t->bar_h - 2,
                                      theme_get_color(THEME_ROLE_MENU_ITEM_HOVER), 120);
        int ty = (t->bar_h - tlh(TB_TEXT_PX)) / 2 + 1;
        ttext(t->cpu_x + 10, ty, cpu, mut, TB_TEXT_PX);
        /* value part re-drawn brighter */
        const char* sp = cpu;
        while (*sp && *sp != ' ') sp++;
        if (*sp == ' ') {
            int vx = t->cpu_x + 10 + twidth(cpu, TB_TEXT_PX) - twidth(sp + 1, TB_TEXT_PX);
            ttext(vx, ty, sp + 1, txt, TB_TEXT_PX);
        }
    }

    /* RAM module */
    if (L->monitor && t->ram_w > 0) {
        int hovr = pir(mx, my, t->ram_x, 0, t->ram_w, t->bar_h);
        if (hovr) gfx_fill_rect_alpha(t->ram_x, 1, t->ram_w, t->bar_h - 2,
                                      theme_get_color(THEME_ROLE_MENU_ITEM_HOVER), 120);
        int ty = (t->bar_h - tlh(TB_TEXT_PX)) / 2 + 1;
        ttext(t->ram_x + 10, ty, ram, mut, TB_TEXT_PX);
        const char* sp = ram;
        while (*sp && *sp != ' ') sp++;
        if (*sp == ' ') {
            int vx = t->ram_x + 10 + twidth(ram, TB_TEXT_PX) - twidth(sp + 1, TB_TEXT_PX);
            ttext(vx, ty, sp + 1, gfx_darken(0x4FA96B, 30), TB_TEXT_PX);
        }
    }

    /* Clock */
    if (L->show_clock && t->clk_w > 0) {
        int hovr = pir(mx, my, t->clk_x, 0, t->clk_w, t->bar_h);
        if (hovr) gfx_fill_rect_alpha(t->clk_x, 1, t->clk_w, t->bar_h - 2,
                                      theme_get_color(THEME_ROLE_MENU_ITEM_HOVER), 120);
        int cw = twidth(clk, TB_TEXT_PX);
        int ty = (t->bar_h - tlh(TB_TEXT_PX)) / 2 + 1;
        ttext(t->clk_x + (t->clk_w - cw) / 2, ty, clk, txt, TB_TEXT_PX);
    }

    /* Power glyph */
    int pcx = t->pwr_x + t->pwr_w / 2, pcy = t->bar_h / 2;
    int hovr = pir(mx, my, t->pwr_x, 0, t->pwr_w, t->bar_h);
    if (hovr || g_pwpop)
        gfx_fill_rect(t->pwr_x, 1, t->pwr_w, t->bar_h - 2,
                      g_pwpop ? theme_get_color(THEME_ROLE_ERROR)
                              : theme_get_color(THEME_ROLE_MENU_ITEM_HOVER));
    if (!icon_theme_draw("#power", pcx - 7, pcy - 7, 14)) {
        int r = t->bar_h >= 25 ? 4 : 3;
        uint32_t pc = (g_pwpop) ? 0xFFFFFF : txt;
        gfx_draw_circle(pcx, pcy + 1, r, pc);
        if (r >= 4) gfx_draw_circle(pcx, pcy + 1, r - 1, pc);
        gfx_fill_rect(pcx, pcy - r - 3, 1, r + 1, pc);
    }
}

static void draw_power_popover(const tb_geom_t* t) {
    if (!g_pwpop) return;
    uint32_t panel  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t txt    = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t hov    = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t err    = theme_get_color(THEME_ROLE_ERROR);
    int mx = mouse_get_x(), my = mouse_get_y();

    int pw = 150, ph = 84;
    int px = t->pwr_x + t->pwr_w - pw;
    int py = t->bar_h + 6;
    gfx_draw_shadow(px, py, pw, ph, 10);
    gfx_fill_rect(px, py, pw, ph, panel);
    gfx_draw_rect_outline(px, py, pw, ph, 1, border);

    const char* items[2] = { "Reboot", "Shut Down" };
    const char* iglyph[2] = { "#reboot", "#power" };
    for (int i = 0; i < 2; i++) {
        int iy = py + 8 + i * 34;
        int hovr = pir(mx, my, px + 6, iy, pw - 12, 30);
        if (hovr) gfx_fill_rect(px + 6, iy, pw - 12, 30, hov);
        icon_theme_draw(iglyph[i], px + 12, iy + 7, 16);
        ttext(px + 38, iy + (30 - tlh(13)) / 2 + 1, items[i],
              i == 1 ? err : txt, 13);
    }
}

/* ── Haiku-style cascading start menu ──────────────────────── */
static void draw_chevron(int x, int y, uint32_t c) {
    gfx_draw_line(x, y, x + 3, y + 4, c);
    gfx_draw_line(x + 3, y + 4, x, y + 8, c);
    gfx_draw_line(x + 1, y, x + 4, y + 4, c);
    gfx_draw_line(x + 4, y + 4, x + 1, y + 8, c);
}

static void draw_cascade(void) {
    if (!g_menu) return;
    cm_geom_t mg; int n = cm_build(&mg);
    uint32_t panel  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t txt    = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t mut    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t hov    = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t selc   = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t acc    = get_accent_color();
    int mx = mouse_get_x(), my = mouse_get_y();

    /* slide-in */
    int oy = 0;
    uint32_t dt = timer_get_ms() - g_open_ms;
    if (dt < 110) oy = -(int)(6 * (110 - dt) / 110);

    gfx_draw_shadow(mg.x, mg.y + oy, mg.w, mg.h, 12);
    gfx_fill_rect(mg.x, mg.y + oy, mg.w, mg.h, panel);
    gfx_draw_rect_outline(mg.x, mg.y + oy, mg.w, mg.h, 1, border);

    /* ── banner: small lambda mark + wordmark ── */
    int bcx = mg.x + 26, bcy = mg.y + oy + CM_BANNER_H / 2 + 1;
    draw_start_glyph(bcx, bcy, 10);
    int name_x = g_sicon_ok ? mg.x + 48 : mg.x + 42;
    ttext(name_x, bcy - tlh(13) / 2, "EigenOS", txt, 13);
    const char* arch = "x86_64";
    ttext(mg.x + mg.w - twidth(arch, 9) - 12,
          bcy - tlh(9) / 2, arch, mut, 9);
    gfx_draw_hline(mg.x + 1, mg.y + oy + CM_BANNER_H, mg.w - 2, border);

    /* hover-follow: hovering a different row switches the submenu */
    int hov_row = -1;
    for (int i = 0; i < n; i++) {
        int rx, ry, rw, rh;
        cm_item_rect(&mg, i, &rx, &ry, &rw, &rh);
        if (pir(mx, my, rx, ry + oy, rw, rh)) { hov_row = i; break; }
    }
    if (hov_row >= 0 && hov_row < n) {
        if (g_menu_sub != mg.cats[hov_row] || g_ms_sub < 0) {
            g_menu_sub = mg.cats[hov_row];
            g_ms_sub   = 0;
        }
        g_ms_main = hov_row;
    }

    /* ── category rows ── */
    int counts[8];
    cat_counts(counts);
    for (int i = 0; i < n; i++) {
        int rx, ry, rw, rh;
        cm_item_rect(&mg, i, &rx, &ry, &rw, &rh);
        ry += oy;
        int cat = mg.cats[i];
        int active = (g_menu_sub == cat);
        int hovr = pir(mx, my, rx, ry, rw, rh);
        if (active || hovr)
            gfx_fill_rect(rx, ry, rw, rh, active ? selc : hov);
        if (active) gfx_fill_rect(rx, ry + 5, 2, rh - 10, acc);

        int ty = ry + (rh - tlh(TB_TEXT_PX)) / 2 + 1;
        static const char* const ck[8] = { "#cat-all", "#cat-productivity",
            "#cat-system", "#cat-games", "#cat-graphics", "#cat-debug",
            "#cat-accessibility", "#cat-networking" };
        int has_ic = icon_theme_draw(ck[cat], rx + 9, ry + (rh - 15) / 2, 15);
        ttext_trunc(rx + (has_ic ? 33 : 12), ty, rw - (has_ic ? 67 : 46),
                    tb_cat_names[cat],
                    active ? txt : (hovr ? txt : mut), TB_TEXT_PX);
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%d", counts[cat]);
        int cwd = twidth(cnt, TB_SMALL_PX);
        ttext(rx + rw - cwd - 20, ry + (rh - tlh(TB_SMALL_PX)) / 2 + 1,
              cnt, mut, TB_SMALL_PX);
        draw_chevron(rx + rw - 14, ry + rh / 2 - 4,
                     active ? acc : mut);
    }

    /* ── fly-out submenu with the apps of the open category ── */
    if (g_menu_sub >= 0) {
        int anchor = 0;
        for (int i = 0; i < n; i++)
            if (mg.cats[i] == g_menu_sub) { anchor = i; break; }
        sm_geom_t sg;
        sm_build(g_menu_sub, &mg, anchor, &sg);

        gfx_draw_shadow(sg.x, sg.y, sg.w, sg.h, 12);
        gfx_fill_rect(sg.x, sg.y, sg.w, sg.h, panel);
        gfx_draw_rect_outline(sg.x, sg.y, sg.w, sg.h, 1, border);

        for (int j = 0; j < sg.n; j++) {
            app_item_t* app = &menu_app_entries[sg.items[j]];
            int rx, ry, rw, rh;
            sm_item_rect(&sg, j, &rx, &ry, &rw, &rh);
            int hovr  = pir(mx, my, rx, ry, rw, rh);
            int ksel  = (j == g_ms_sub);
            if (hovr)  { g_ms_sub = j; }
            if (ksel || hovr)
                gfx_fill_rect(rx, ry, rw, rh, ksel ? selc : hov);
            draw_app_icon_tile(app->name, rx + 6, ry + (rh - 17) / 2, 17,
                               ksel ? 2 : 0, acc);
            ttext_trunc(rx + 30, ry + (rh - tlh(TB_TEXT_PX)) / 2 + 1,
                        rw - 38, app->name,
                        (hovr || ksel) ? txt : theme_get_color(THEME_ROLE_PRIMARY),
                        TB_TEXT_PX);
        }
    }
}

/* ── start-button icon picker (right-click the lambda) ─────── */
static int pick_rows(void) { return g_icon_n + 1; }   /* + Default */

static void pick_geom(int* px, int* py, int* pw, int* ph) {
    tb_geom_t t; tb_geom(&t);
    uint32_t fh = get_fb_height();
    *px = CM_X;
    *py = t.bar_h + 4;
    *pw = 216;
    *ph = 24 + pick_rows() * 24 + 8;
    if (*py + *ph > (int)fh - 8) *ph = (int)fh - 8 - *py;
}

static void pick_row_rect(int i, int px, int py, int* rx, int* ry, int* rw, int* rh) {
    *rx = px + 6;
    *ry = py + 24 + i * 24;
    *rw = 216 - 12;
    *rh = 22;
}

static void draw_icon_picker(void) {
    if (!g_iconpick) return;
    int px, py, pw, ph;
    pick_geom(&px, &py, &pw, &ph);
    uint32_t panel  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t txt    = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t mut    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t hov    = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t selc   = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    int mx = mouse_get_x(), my = mouse_get_y();

    gfx_draw_shadow(px, py, pw, ph, 10);
    gfx_fill_rect(px, py, pw, ph, panel);
    gfx_draw_rect_outline(px, py, pw, ph, 1, border);
    ttext(px + 12, py + 6, "Start icon", mut, 10);
    gfx_draw_hline(px + 1, py + 22, pw - 2, border);

    for (int i = 0; i < pick_rows(); i++) {
        int rx, ry, rw, rh;
        pick_row_rect(i, px, py, &rx, &ry, &rw, &rh);
        int hovr  = pir(mx, my, rx, ry, rw, rh);
        int isdef = (i == 0);
        sicon_ensure();
        int cur  = isdef ? !g_sicon_ok
                         : (g_sicon_ok &&
                            strstr(g_sicon_path, g_icon_names[i - 1]) != NULL);
        if (hovr || cur) gfx_fill_rect(rx, ry, rw, rh, cur ? selc : hov);
        int ty = ry + (rh - tlh(TB_TEXT_PX)) / 2 + 1;
        if (isdef) {
            draw_lambda_mark(rx + 12, ry + rh / 2, 6, get_accent_color(),
                             gfx_lighten(get_accent_color(), 45));
            ttext_trunc(rx + 26, ty, rw - 34, "Default (lambda)",
                        cur ? txt : (hovr ? txt : mut), TB_TEXT_PX);
        } else {
            char disp[48];
            snprintf(disp, sizeof(disp), "%s", g_icon_names[i - 1]);
            int dl = 0; while (disp[dl]) dl++;
            if (dl > 4) disp[dl - 4] = 0;
            ttext_trunc(rx + 12, ty, rw - 20, disp,
                        cur ? txt : (hovr ? txt : mut), TB_TEXT_PX);
        }
    }
}

/* ═══════════════════════ GRID LAUNCHER (search) ════════════ */

static void draw_launcher(void) {
    if (!g_launcher) return;
    lc_geom_t L; lc_geom(&L);
    uint32_t panel = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t txt = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t mut = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t hov = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t selc = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t acc = get_accent_color();
    int mx = mouse_get_x(), my = mouse_get_y();

    /* slide-in animation (120 ms) */
    int slide = 0;
    uint32_t dt = timer_get_ms() - g_open_ms;
    if (dt < 120) slide = (int)(12 * (120 - dt) / 120);
    int ox = 0, oy = -slide;

    gfx_draw_shadow(L.x, L.y, L.w, L.h, 14);
    gfx_fill_rect(L.x, L.y + oy, L.w, L.h, panel);
    gfx_draw_rect_outline(L.x, L.y + oy, L.w, L.h, 1, border);

    /* ── header: search field ── */
    int sf_x = L.x + 12, sf_y = L.y + oy + 12;
    int sf_w = L.w - 24, sf_h = LAUNCHER_HEAD_H - 22;
    gfx_fill_rect(sf_x, sf_y, sf_w, sf_h,
                  theme_get_color(THEME_ROLE_SURFACE_VARIANT));
    gfx_draw_rect_outline(sf_x, sf_y, sf_w, sf_h, 1,
                          g_qlen > 0 ? acc : border);
    draw_search_icon(sf_x + 10, sf_y + (sf_h - 20) / 2, 20, 20);
    if (g_qlen > 0) {
        ttext(sf_x + 40, sf_y + (sf_h - tlh(14)) / 2 + 1, g_q, txt, 14);
    } else {
        ttext(sf_x + 40, sf_y + (sf_h - tlh(14)) / 2 + 1, "Search apps...", mut, 14);
    }
    /* caret blink */
    if ((timer_get_ms() / 500) % 2 == 0) {
        int cxp = sf_x + 40 + twidth(g_q, 14) + 2;
        gfx_fill_rect(cxp, sf_y + 8, 2, sf_h - 16, acc);
    }

    /* ── sidebar categories ── */
    int cat_counts_[8] = { 0 };
    for (int i = 0; menu_app_entries[i].name != 0; i++) {
        cat_counts_[0]++;
        if (menu_app_entries[i].category >= 1 && menu_app_entries[i].category <= 7)
            cat_counts_[menu_app_entries[i].category]++;
    }
    for (int c = 0; c < 8; c++) {
        int cy = L.side_y + c * 32;
        if (cy + 28 > L.foot_y) break;
        int hovr = pir(mx, my, L.side_x, cy, L.side_w, 28);
        if (c == g_cat)
            gfx_fill_rect(L.side_x, cy + oy, L.side_w, 28, selc);
        else if (hovr)
            gfx_fill_rect(L.side_x, cy + oy, L.side_w, 28, hov);
        ttext_trunc(L.side_x + 12, cy + oy + (28 - tlh(13)) / 2 + 1,
                    L.side_w - 38, tb_cat_names[c],
                    c == g_cat ? 0xFFFFFF : (hovr ? txt : mut), 13);
        char cnt[8];
        cnt[0] = 0;
        snprintf(cnt, sizeof(cnt), "%d", cat_counts_[c]);
        int cwd = twidth(cnt, 11);
        ttext(L.side_x + L.side_w - cwd - 10, cy + oy + (28 - tlh(11)) / 2 + 1,
              cnt, mut, 11);
    }

    /* ── grid cards ── */
    int rows = (g_nfiltered + L.cols - 1) / L.cols;
    int rows_vis = L.grid_h / (CARD_H + CARD_GAP);
    if (rows_vis < 1) rows_vis = 1;
    if (g_scroll > rows - rows_vis) g_scroll = rows - rows_vis;
    if (g_scroll < 0) g_scroll = 0;

    gfx_push_clip(L.grid_x, L.grid_y + oy, L.grid_w, L.grid_h);
    for (int vi = 0; vi < g_nfiltered; vi++) {
        int idx = vi - g_scroll * L.cols;
        if (idx < 0) continue;
        int r = idx / L.cols, col = idx % L.cols;
        if (r >= rows_vis) continue;
        int cx = L.grid_x + col * (CARD_W + CARD_GAP);
        int cy = L.grid_y + r * (CARD_H + CARD_GAP);
        app_item_t* app = &menu_app_entries[g_filtered[vi]];

        int hovr = pir(mx, my, cx, cy, CARD_W, CARD_H);
        int ksel = (vi == g_sel);
        if (ksel || hovr) {
            gfx_fill_rect(cx, cy + oy, CARD_W, CARD_H, hov);
            if (ksel) gfx_draw_rect_outline(cx, cy + oy, CARD_W, CARD_H, 2, acc);
            else gfx_draw_rect_outline(cx, cy + oy, CARD_W, CARD_H, 1, border);
        }
        draw_app_icon_tile(app->name, cx + 10, cy + oy + (CARD_H - 44) / 2, 44,
                           ksel ? 2 : 0, acc);
        ttext_trunc(cx + 62, cy + oy + (CARD_H - tlh(13)) / 2 + 1,
                    CARD_W - 70, app->name, hovr ? txt : (ksel ? txt : theme_get_color(THEME_ROLE_PRIMARY)), 13);
    }
    gfx_pop_clip();

    /* scrollbar */
    if (rows > rows_vis) {
        int sb_x = L.x + L.w - 8;
        int sb_y = L.grid_y + oy;
        int sb_h = L.grid_h;
        gfx_fill_rect(sb_x, sb_y, 4, sb_h, theme_get_color(THEME_ROLE_SCROLLBAR));
        int thumb_h = sb_h * rows_vis / rows;
        int thumb_y = sb_y + sb_h * g_scroll / rows;
        gfx_fill_rect(sb_x, thumb_y, 4, thumb_h, acc);
    }

    /* ── footer ── */
    gfx_draw_hline(L.x, L.foot_y + oy, L.w, border);
    char foot[96];
    snprintf(foot, sizeof(foot), "%d apps", g_nfiltered);
    ttext(L.x + 12, L.foot_y + oy + (LAUNCHER_FOOT_H - tlh(11)) / 2 + 1,
          foot, mut, 11);
    const char* hint = "Enter launch   Esc close   type to search";
    int hw = twidth(hint, 11);
    ttext(L.x + L.w - hw - 12, L.foot_y + oy + (LAUNCHER_FOOT_H - tlh(11)) / 2 + 1,
          hint, mut, 11);
}

/* ═══════════════════════ BAR RENDER ════════════════════════ */

static void draw_menu_button(const tb_geom_t* t) {
    int mx = mouse_get_x(), my = mouse_get_y();
    int hovr = pir(mx, my, t->menu_x, 0, t->menu_w, t->bar_h);

    if (g_menu || hovr)
        gfx_fill_rect(t->menu_x, 1, t->menu_w, t->bar_h - 2,
                      theme_get_color(THEME_ROLE_MENU_ITEM_HOVER));

    draw_start_glyph(t->menu_x + t->menu_w / 2, t->bar_h / 2, t->bar_h / 4);

}

/* context menu (right-click on a taskbar slot) */
static void draw_ctx_menu(void) {
    if (!g_ctx) return;
    uint32_t panel  = theme_get_color(THEME_ROLE_MENU_BG);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t txt    = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t hov    = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    int mx = mouse_get_x(), my = mouse_get_y();

    int pinned = -1;
    for (int i = 0; menu_app_entries[i].name; i++)
        if (strcmp(menu_app_entries[i].name, g_ctx_app) == 0)
            pinned = app_pinned(menu_app_entries[i].global_idx);

    struct { const char* key; const char* label; int danger; } it[3];
    int n = 0;
    it[n].key="#plus";   it[n].label="Open";           it[n].danger=0; n++;
    if (pinned >= 0) {
        if (pinned) { it[n].key="#unpin"; it[n].label="Unpin"; }
        else        { it[n].key="#pin";   it[n].label="Pin to taskbar"; }
        it[n].danger=0; n++;
    }
    if (g_ctx_win >= 0) { it[n].key="#close"; it[n].label="Close window"; it[n].danger=1; n++; }
    g_ctx_rows = n;

    int w=182,h=n*26+10,x=g_ctx_x,y=g_ctx_y;
    uint32_t fw=get_fb_width(),fh=get_fb_height();
    if(x+w>(int)fw-4)x=(int)fw-4-w;
    if(y+h>(int)fh-4)y=(int)fh-4-h;
    g_ctx_x=x; g_ctx_y=y;
    gfx_draw_shadow(x,y,w,h,10);
    gfx_fill_rect(x,y,w,h,panel);
    gfx_draw_rect_outline(x,y,w,h,1,border);
    for(int i=0;i<n;i++){
        int ry=y+5+i*26,rh=22;
        if(pir(mx,my,x+4,ry,w-8,rh))gfx_fill_rect(x+4,ry,w-8,rh,hov);
        icon_theme_draw(it[i].key,x+10,ry+4,14);
        ttext(x+32,ry+(rh-tlh(11))/2,it[i].label,
              it[i].danger?theme_get_color(THEME_ROLE_ERROR):txt,11);
    }
}

/* middle-click closes a window-button's window (edge detected here) */
static void taskbar_midclick(void);

void draw_taskbar_mac(void) {
    uint32_t fw = get_fb_width();
    if (fw == 0) return;

    taskbar_midclick();

    tb_geom_t t;
    tb_geom(&t);
    layout_t* L = gui_get_layout();

    uint32_t bg     = theme_get_color(THEME_ROLE_TASKBAR_BG);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t mut    = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t acc    = get_accent_color();
    int mx = mouse_get_x(), my = mouse_get_y();

    /* flat bar surface */
    gfx_fill_rect(0, 0, (int)fw, t.bar_h, bg);

    if (L->show_toolbar) {
        draw_menu_button(&t);

        /* virtual desktop digits */
        int cur_desk = wm_get_current_desktop();
        for (int d = 0; d < TB_DESKTOPS; d++) {
            int dx = t.desk_x + d * TB_DESK_SEG_W;
            char num[2] = { (char)('1' + d), 0 };
            int nw = twidth(num, TB_SMALL_PX);
            int hovr = pir(mx, my, dx, 0, TB_DESK_SEG_W, t.bar_h);
            if (d == cur_desk) {
                ttext(dx + (TB_DESK_SEG_W - nw) / 2,
                      (t.bar_h - tlh(TB_SMALL_PX)) / 2, num, acc, TB_SMALL_PX);
                gfx_fill_rect(dx + 3, t.bar_h - 3, TB_DESK_SEG_W - 6, 2, acc);
            } else {
                if (hovr)
                    gfx_fill_rect_alpha(dx, 1, TB_DESK_SEG_W, t.bar_h - 2,
                                        theme_get_color(THEME_ROLE_MENU_ITEM_HOVER), 120);
                ttext(dx + (TB_DESK_SEG_W - nw) / 2,
                      (t.bar_h - tlh(TB_SMALL_PX)) / 2, num, mut, TB_SMALL_PX);
            }
        }
    }

    if (L->show_toolbar) draw_window_buttons(&t);
    draw_tray(&t);
    draw_power_popover(&t);
    draw_cascade();
    draw_ctx_menu();
    draw_icon_picker();
    draw_launcher();

    /* wheel scrolling inside the launcher grid */
    if (g_launcher) {
        int wd = mouse_get_wheel_delta();
        if (wd != 0) {
            mouse_clear_wheel_delta();
            lc_geom_t Lc; lc_geom(&Lc);
            int rows_vis = Lc.grid_h / (CARD_H + CARD_GAP);
            if (rows_vis < 1) rows_vis = 1;
            g_scroll += (wd > 0 ? -rows_vis : rows_vis);
            if (g_scroll < 0) g_scroll = 0;
        }
    }
}

/* ═══════════════════════ INPUT ═════════════════════════════ */

int taskbar_handle_click(int cx, int cy, int clicked, int rclicked) {
    if (!clicked && !rclicked) return 0;

    tb_geom_t t;
    tb_geom(&t);
    int in_bar = pir(cx, cy, 0, 0, (int)get_fb_width(), t.bar_h);

    /* ── context menu (top priority) ── */
    if (g_ctx) {
        int w = 182, h = g_ctx_rows * 26 + 10;
        if (!pir(cx, cy, g_ctx_x, g_ctx_y, w, h)) { g_ctx = 0; return 1; }
        int idx = (cy >= g_ctx_y + 5) ? (cy - g_ctx_y - 5) / 26 : -1;
        const app_item_t* app = 0;
        for (int i = 0; menu_app_entries[i].name; i++)
            if (strcmp(menu_app_entries[i].name, g_ctx_app) == 0)
                { app = &menu_app_entries[i]; break; }
        if (idx >= 0 && idx < g_ctx_rows && app) {
            if (idx == 0) { launch_item(app->global_idx); g_ctx = 0; return 1; }
            if (idx == 1 && g_ctx_win < 0) {
                set_pin(app->global_idx, !app_pinned(app->global_idx));
                g_ctx = 0; return 1;
            }
            if (idx == 2 && g_ctx_win >= 0) {
                wm_close_window((uint32_t)g_ctx_win); g_ctx = 0; return 1;
            }
        }
        g_ctx = 0; return 1;
    }

    /* ── icon picker ── */
    if (g_iconpick) {
        int px, py, pw, ph;
        pick_geom(&px, &py, &pw, &ph);
        int inside = pir(cx, cy, px, py, pw, ph);
        if (!inside && !in_bar) { g_iconpick = 0; return 1; }
        if (in_bar && !pir(cx, cy, t.menu_x, 0, t.menu_w, t.bar_h)) {
            g_iconpick = 0;
            /* fall through to normal bar handling */
        } else if (inside) {
            for (int i = 0; i < pick_rows(); i++) {
                int rx, ry, rw, rh;
                pick_row_rect(i, px, py, &rx, &ry, &rw, &rh);
                if (!pir(cx, cy, rx, ry, rw, rh)) continue;
                if (i == 0) sicon_apply("");
                else {
                    char path[160];
                    snprintf(path, sizeof(path), ICON_DIR "/%s", g_icon_names[i - 1]);
                    sicon_apply(path);
                }
                g_iconpick = 0;
                return 1;
            }
            return 1;   /* consumed anywhere inside the picker */
        }
        if (pir(cx, cy, t.menu_x, 0, t.menu_w, t.bar_h)) {
            g_iconpick = 0;   /* toggle off via the button */
            return 1;
        }
    }

    /* ── cascade start menu ── */
    if (g_menu) {
        cm_geom_t mg;
        cm_build(&mg);
        int in_panel = pir(cx, cy, mg.x, mg.y, mg.w, mg.h);

        /* submenu (if open) */
        if (g_menu_sub >= 0) {
            int anchor = 0;
            for (int i = 0; i < mg.items_n; i++)
                if (mg.cats[i] == g_menu_sub) { anchor = i; break; }
            sm_geom_t sg;
            sm_build(g_menu_sub, &mg, anchor, &sg);
            if (pir(cx, cy, sg.x, sg.y, sg.w, sg.h)) {
                for (int j = 0; j < sg.n; j++) {
                    int rx, ry, rw, rh;
                    sm_item_rect(&sg, j, &rx, &ry, &rw, &rh);
                    if (pir(cx, cy, rx, ry, rw, rh)) {
                        launch_menu_app(sg.items[j]);
                        return 1;
                    }
                }
                return 1;   /* inside submenu but between rows */
            }
        }

        if (rclicked) { taskbar_launcher_close(); return 1; }

        if (in_panel) {
            if (cy >= mg.y + 46 + CM_PAD) {          /* below the banner */
                for (int i = 0; i < mg.items_n; i++) {
                    int rx, ry, rw, rh;
                    cm_item_rect(&mg, i, &rx, &ry, &rw, &rh);
                    if (!pir(cx, cy, rx, ry, rw, rh)) continue;
                    if (g_menu_sub == mg.cats[i]) {  /* toggle closed */
                        g_menu_sub = -1; g_ms_sub = -1;
                    } else {
                        g_menu_sub = mg.cats[i];
                        g_ms_sub = 0;
                    }
                    g_ms_main = i;
                    return 1;
                }
            }
            return 1;   /* clicks inside the panel never fall through */
        }

        if (!in_bar) { taskbar_launcher_close(); return 1; }

        if (pir(cx, cy, t.menu_x, 0, t.menu_w, t.bar_h)) {
            taskbar_launcher_close();   /* button toggles the menu off */
            return 1;
        }
        /* other bar clicks: dismiss the menu, then process normally */
        taskbar_launcher_close();
    }

    /* ── power popover ── */
    if (g_pwpop) {
        int pw = 150, ph = 84;
        int px = t.pwr_x + t.pwr_w - pw;
        int py = t.bar_h + 6;
        for (int i = 0; i < 2; i++) {
            int iy = py + 8 + i * 34;
            if (pir(cx, cy, px + 6, iy, pw - 12, 30)) {
                taskbar_launcher_close();
                if (i == 0) acpi_reboot();
                else gui_system_shutdown();
                return 1;
            }
        }
        if (!pir(cx, cy, px, py, pw, ph) && !in_bar) { g_pwpop = 0; return 1; }
        if (pir(cx, cy, t.pwr_x, 0, t.pwr_w, t.bar_h)) { g_pwpop = 0; return 1; }
        g_pwpop = 0;
        return 1;
    }

    /* ── searchable grid launcher ── */
    if (g_launcher) {
        lc_geom_t L;
        lc_geom(&L);
        int in_panel = pir(cx, cy, L.x, L.y, L.w, L.h);

        if (!in_panel && !in_bar) {
            taskbar_launcher_close();
            return 1;
        }

        if (in_panel) {
            for (int c = 0; c < 8; c++) {
                int cy2 = L.side_y + c * 32;
                if (pir(cx, cy, L.side_x, cy2, L.side_w, 28)) {
                    if (g_cat != c) { g_cat = c; g_scroll = 0; g_sel = -1; refilter(); }
                    return 1;
                }
            }
            refilter();
            int rows_vis = L.grid_h / (CARD_H + CARD_GAP);
            if (rows_vis < 1) rows_vis = 1;
            for (int vi = 0; vi < g_nfiltered; vi++) {
                int idx = vi - g_scroll * L.cols;
                if (idx < 0) continue;
                int r = idx / L.cols, col = idx % L.cols;
                if (r >= rows_vis) continue;
                int ccx = L.grid_x + col * (CARD_W + CARD_GAP);
                int ccy = L.grid_y + r * (CARD_H + CARD_GAP);
                if (pir(cx, cy, ccx, ccy, CARD_W, CARD_H)) {
                    launch_grid_item(vi);
                    return 1;
                }
            }
            return 1;
        }
    }

    /* ── bar ── */
    if (!in_bar) return 0;

    /* right-click on the lambda: import / choose the start icon */
    if (rclicked && pir(cx, cy, t.menu_x, 0, t.menu_w, t.bar_h)) {
        icons_scan();
        g_ipsel = 0;
        g_iconpick = 1;
        g_pwpop = 0; g_menu = 0; g_launcher = 0;
        return 1;
    }

    /* menu button toggles the cascade start menu */
    if (pir(cx, cy, t.menu_x, 0, t.menu_w, t.bar_h)) {
        taskbar_menu_toggle();
        return 1;
    }

    /* desktop pills */
    for (int d = 0; d < TB_DESKTOPS; d++) {
        int dx = t.desk_x + d * TB_DESK_SEG_W;
        if (pir(cx, cy, dx, 0, TB_DESK_SEG_W, t.bar_h)) {
            wm_set_current_desktop(d);
            return 1;
        }
    }

    /* taskbar slots: static pin launches; right-click opens context menu */
    {
        tslot_t sl[WM_MAX_WINDOWS + 16];
        int xs[WM_MAX_WINDOWS + 16], ws_[WM_MAX_WINDOWS + 16];
        int count = tb_slots(&t, sl, WM_MAX_WINDOWS + 16, xs, ws_);
        for (int i = 0; i < count; i++) {
            int wx = xs[i], bw = ws_[i];
            if (!pir(cx, cy, wx, 0, bw, t.bar_h)) continue;
            if (rclicked) { ctx_open(cx, cy, sl[i].app, sl[i].win); return 1; }
            if (sl[i].is_static) { launch_item(sl[i].app->global_idx); return 1; }
            wm_window_t* win = sl[i].win;
            if (win->flags & WM_FLAG_MINIMIZED) {
                wm_restore_window(win->id); wm_bring_to_front(win->id);
            } else if (win->id == wm_get_focused()) {
                wm_minimize_window(win->id);
            } else {
                wm_bring_to_front(win->id);
            }
            g_pwpop = 0;
            return 1;
        }
    }

    /* tray: clock/power */
    if (pir(cx, cy, t.clk_x, 0, t.clk_w, t.bar_h)) {
        taskbar_launcher_close();
        return 1;
    }
    if (pir(cx, cy, t.pwr_x, 0, t.pwr_w, t.bar_h)) {
        g_pwpop = !g_pwpop;
        return 1;
    }

    /* any other bar click: just consumed (no fall-through to windows) */
    return 1;
}

/* middle-click closes a window-button's window (edge detected here) */
static void taskbar_midclick(void) {
    int mbtn = mouse_get_buttons();
    int mid_now = (mbtn & 4) != 0;
    int mid_edge = mid_now && !g_prev_mid;
    g_prev_mid = mbtn;
    if (!mid_edge) return;

    tb_geom_t t;
    tb_geom(&t);
    int mx = mouse_get_x(), my = mouse_get_y();
    if (my >= t.bar_h) return;

    tslot_t sl[WM_MAX_WINDOWS + 16];
    int xs[WM_MAX_WINDOWS + 16], ws_[WM_MAX_WINDOWS + 16];
    int count = tb_slots(&t, sl, WM_MAX_WINDOWS + 16, xs, ws_);
    for (int i = 0; i < count; i++) {
        int wx = xs[i], bw = ws_[i];
        if (sl[i].win && pir(mx, my, wx, 0, bw, t.bar_h)) {
            wm_close_window(sl[i].win->id);
            return;
        }
    }
}

/* ═══════════════════════ KEYBOARD ══════════════════════════ */

int taskbar_handle_key(char key_in) {
    unsigned char key = (unsigned char)key_in;

    /* ── icon picker ── */
    if (g_iconpick) {
        int rows = pick_rows();
        if (KEY_MATCH(key, KEY_ESC)) { g_iconpick = 0; return 1; }
        if (KEY_MATCH(key, '\n')) {
            if (g_ipsel == 0) sicon_apply("");
            else {
                char path[160];
                snprintf(path, sizeof(path), ICON_DIR "/%s", g_icon_names[g_ipsel - 1]);
                sicon_apply(path);
            }
            g_iconpick = 0;
            return 1;
        }
        if (KEY_MATCH(key, KEY_DOWN)) { if (g_ipsel < rows - 1) g_ipsel++; return 1; }
        if (KEY_MATCH(key, KEY_UP))   { if (g_ipsel > 0) g_ipsel--; return 1; }
        return 1;
    }

    /* ── cascade start menu ── */
    if (g_menu) {
        cm_geom_t mg;
        int n = cm_build(&mg);

        if (KEY_MATCH(key, KEY_ESC)) { taskbar_launcher_close(); return 1; }

        if (g_menu_sub < 0) {
            if (KEY_MATCH(key, KEY_DOWN)) {
                if (g_ms_main < n - 1) g_ms_main++; else g_ms_main = 0;
                return 1;
            }
            if (KEY_MATCH(key, KEY_UP)) {
                if (g_ms_main > 0) g_ms_main--; else g_ms_main = n - 1;
                return 1;
            }
            if (KEY_MATCH(key, KEY_RIGHT) || KEY_MATCH(key, '\n')) {
                if (g_ms_main >= 0 && g_ms_main < n) {
                    g_menu_sub = mg.cats[g_ms_main];
                    g_ms_sub = 0;
                }
                return 1;
            }
            return 1;   /* swallow everything while the menu is open */
        }

        /* submenu navigation */
        sm_geom_t sg;
        int anchor = 0;
        for (int i = 0; i < n; i++)
            if (mg.cats[i] == g_menu_sub) { anchor = i; break; }
        sm_build(g_menu_sub, &mg, anchor, &sg);

        if (KEY_MATCH(key, KEY_ESC)) { taskbar_launcher_close(); return 1; }
        if (KEY_MATCH(key, KEY_LEFT)) { g_menu_sub = -1; g_ms_sub = -1; return 1; }
        if (KEY_MATCH(key, '\n')) {
            if (g_ms_sub >= 0 && g_ms_sub < sg.n) launch_menu_app(sg.items[g_ms_sub]);
            return 1;
        }
        if (KEY_MATCH(key, KEY_DOWN)) { if (g_ms_sub < sg.n - 1) g_ms_sub++; return 1; }
        if (KEY_MATCH(key, KEY_UP))   { if (g_ms_sub > 0) g_ms_sub--; return 1; }
        return 1;
    }

    if (!g_launcher) return 0;

    /* ── searchable grid launcher ── */
    if (KEY_MATCH(key, KEY_ESC)) { taskbar_launcher_close(); return 1; }

    if (KEY_MATCH(key, '\b')) {
        if (g_qlen > 0) { g_q[--g_qlen] = 0; refilter(); g_sel = -1; g_scroll = 0; }
        return 1;
    }

    if (KEY_MATCH(key, '\n')) {
        int target = (g_sel >= 0 && g_sel < g_nfiltered) ? g_sel : (g_nfiltered > 0 ? 0 : -1);
        launch_grid_item(target);
        return 1;
    }

    if (KEY_MATCH(key, KEY_DOWN)) { if (g_sel < g_nfiltered - 1) g_sel++; return 1; }
    if (KEY_MATCH(key, KEY_UP))   { if (g_sel > 0) g_sel--; return 1; }
    if (KEY_MATCH(key, KEY_LEFT)) { if (g_sel > 0) { g_sel--; } else if (g_nfiltered) g_sel = g_nfiltered - 1; return 1; }
    if (KEY_MATCH(key, KEY_RIGHT)) { if (g_sel < g_nfiltered - 1) g_sel++; else g_sel = 0; return 1; }

    lc_geom_t L; lc_geom(&L);
    int rows_vis = L.grid_h / (CARD_H + CARD_GAP);
    if (rows_vis < 1) rows_vis = 1;
    if (KEY_MATCH(key, KEY_PAGE_DOWN)) { g_scroll += rows_vis; g_sel = -1; return 1; }
    if (KEY_MATCH(key, KEY_PAGE_UP))   { g_scroll -= rows_vis; if (g_scroll < 0) g_scroll = 0; g_sel = -1; return 1; }

    if (key >= 32 && key <= 126 && g_qlen < 62) {
        g_q[g_qlen++] = (char)key;
        g_q[g_qlen] = 0;
        refilter();
        g_sel = -1;
        g_scroll = 0;
        return 1;
    }

    return 1; /* launcher open: swallow everything else so windows don't get it */
}
