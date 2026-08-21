/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Settings (ring 3, EigenUI — no Nuklear)
 *
 * Sections:
 *  0 Personalization — wallpaper picker (BMP thumbnails), accent
 *  1 Themes          — light/dark + custom accent colour swatches
 *  2 About           — OS version, CPU, RAM, uptime
 *  3 Bluetooth/Ports — BT radio, USB hubs, COM ports
 *  4 Screen Saver    — effect + idle timeout
 *  5 Mouse           — (coming soon)
 *  6 Taskbar         — (coming soon)
 *  7 Animations      — (coming soon)
 *  8 Shortcuts       — (coming soon)
 *********************************************************************/
#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── window ── */
#define WIN_W  880
#define WIN_H  580
#define MAX_EVS 64

/* ── layout ── */
#define HEADER_H  40
#define SIDEBAR_W 160
#define FOOTER_H  22

/* ── sections ── */
#define SEC_PERSONALIZATION 0
#define SEC_THEMES          1
#define SEC_ABOUT           2
#define SEC_BLUETOOTH_PORTS 3
#define SEC_SCREENSAVER     4
#define SEC_MOUSE           5
#define SEC_TASKBAR         6
#define SEC_ANIMATIONS      7
#define SEC_SHORTCUTS       8
#define SEC_COUNT           9

/* ── state ── */
static int cur_sec = 0;

/* wallpaper library (mirrors kernel wp_entry_t — fetched via syscalls) */
#define WP_MAX 16
#define THUMB_W 96
#define THUMB_H 54
#define SW_W 64
#define SW_H 38
#define SW_SC_W 60
#define SW_SC_H 34
struct wp_lib_entry { char path[96]; char name[64]; uint32_t last_used; int installed; };
static struct wp_lib_entry g_wp[WP_MAX];
static int g_wp_count = 0;
static int g_wp_sel = -1;          /* index of the currently-applied wallpaper */
static char g_wp_active[96];       /* active wallpaper path (mode 1) */
static uint8_t g_wp_raw[WP_MAX][THUMB_W * THUMB_H * 3];
static int  g_wp_raw_ok[WP_MAX];
static uint32_t g_wp_sc[WP_MAX][SW_SC_W * SW_SC_H];
static int  g_wp_sc_ok[WP_MAX];

static void wp_refresh(void) {
    g_wp_count = 0;
    eigen_settings(EIGEN_SETTINGS_WALL_RESCAN, 0, 0, 0);
    int n = (int)eigen_settings(EIGEN_SETTINGS_WALL_COUNT, 0, 0, 0);
    if (n > WP_MAX) n = WP_MAX;
    for (int i = 0; i < n; i++) {
        if (eigen_settings(EIGEN_SETTINGS_WALL_GET, (uint64_t)i,
                           (uint64_t)(uintptr_t)&g_wp[g_wp_count], 0) == 0)
            g_wp_count++;
    }
    g_wp_sel = -1;
    if ((int)eigen_settings(EIGEN_SETTINGS_WALL_MODE, 0, 0, 0) == 1) {
        eigen_settings(EIGEN_SETTINGS_WALL_PATH, (uint64_t)(uintptr_t)g_wp_active,
                       sizeof(g_wp_active), 0);
        for (int i = 0; i < g_wp_count; i++)
            if (strcmp(g_wp[i].path, g_wp_active) == 0) { g_wp_sel = i; break; }
    }
}

/* fetch + scale the kernel thumbnail once, then blit every frame */
static void wp_thumb_ensure(int i) {
    if (i < 0 || i >= WP_MAX || g_wp_raw_ok[i] != 0) return;
    if (eigen_settings(EIGEN_SETTINGS_WALL_THUMB_RAW, (uint64_t)i,
                       (uint64_t)(uintptr_t)g_wp_raw[i], 0) == 0)
        g_wp_raw_ok[i] = 1;
    else
        g_wp_raw_ok[i] = -1;
    if (g_wp_raw_ok[i] != 1) return;
    for (int yy = 0; yy < SW_SC_H; yy++) {
        int sy = (yy * THUMB_H) / SW_SC_H;
        const uint8_t* r = g_wp_raw[i] + (size_t)sy * THUMB_W * 3;
        uint32_t* d = g_wp_sc[i];
        for (int xx = 0; xx < SW_SC_W; xx++) {
            int sx = (xx * THUMB_W) / SW_SC_W;
            const uint8_t* p = r + (size_t)sx * 3;
            d[(size_t)yy * SW_SC_W + xx] =
                ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
    g_wp_sc_ok[i] = 1;
}
static void wp_thumb_blit(uint32_t* fb, int W, int H, int i, int bx, int by) {
    if (i < 0 || i >= WP_MAX || g_wp_sc_ok[i] != 1) return;
    if (bx < 0 || by < 0 || bx + SW_SC_W > W || by + SW_SC_H > H) return;
    for (int yy = 0; yy < SW_SC_H; yy++) {
        const uint32_t* r = g_wp_sc[i] + (size_t)yy * SW_SC_W;
        uint32_t* d = fb + (size_t)(by + yy) * (size_t)W + bx;
        for (int xx = 0; xx < SW_SC_W; xx++) d[xx] = r[xx];
    }
}

/* accent swatches (kernel accent table) */
#define ACCENT_N 8
static uint32_t g_accents[ACCENT_N];
static int g_accent_sel = 0;
static void accent_refresh(void) {
    for (int i = 0; i < ACCENT_N; i++) {
        int64_t c = (int64_t)(int32_t)eigen_settings(EIGEN_SETTINGS_ACCENT_COLOR,
                                                     (uint64_t)i, 0, 0);
        g_accents[i] = (c <= 0) ? 0x333344 : (uint32_t)c;
    }
    int cur = (int)eigen_settings(EIGEN_SETTINGS_ACCENT_GET, 0, 0, 0);
    g_accent_sel = (cur >= 0 && cur < ACCENT_N) ? cur : 0;
}

/* screensaver */
static int g_saver_fx    = 0;
static int g_saver_delay = 1;

/* ── ui context ── */
static ui_t ui;

/* ─────────────────────────────────────────────────────────────
 *  HELPERS
 * ───────────────────────────────────────────────────────────── */
static void hline(uint32_t* fb, int W, int H, int x, int y, int w, uint32_t col) {
    eigen_draw_fillrect(fb, W, H, x, y, w, 1, col);
}
static void vline(uint32_t* fb, int W, int H, int x, int y, int h, uint32_t col) {
    eigen_draw_fillrect(fb, W, H, x, y, 1, h, col);
}

/* card — raised panel with optional title bar */
static void card(uint32_t* fb, int W, int H, int x, int y, int w, int h,
                 const char* title) {
    uint32_t bg   = ui_theme.panel;
    uint32_t bdr  = ui_theme.border;
    uint32_t hbg  = ui_theme.panel2;
    uint32_t acc  = ui_theme.accent;
    uint32_t txt  = ui_theme.text;

    ui_fill_round(fb, W, H, x, y, w, h, 8, bg);
    ui_draw_round(fb, W, H, x, y, w, h, 8, bdr);

    if (title && title[0]) {
        ui_fill_round(fb, W, H, x, y, w, 28, 8, hbg);
        ui_draw_round(fb, W, H, x, y, w, 28, 8, bdr);
        /* accent left tab */
        eigen_draw_fillrect(fb, W, H, x + 6, y + 8, 3, 12, acc);
        eigen_draw_text(fb, W, H, x + 16, y + 6, title, txt);
    }
}

/* section header */
static void sec_header(uint32_t* fb, int W, int H, int x, int y, const char* title) {
    eigen_draw_fillrect(fb, W, H, x, y, 4, 22, ui_theme.accent);
    eigen_draw_text(fb, W, H, x + 10, y + 3, title, ui_theme.text);
    hline(fb, W, H, x, y + 26, W - x - 8, ui_theme.border);
}

/* small label text */
static void lbl(uint32_t* fb, int W, int H, int x, int y, const char* s, uint32_t c) {
    eigen_draw_text(fb, W, H, x, y, s, c);
}

/* ─────────────────────────────────────────────────────────────
 *  SECTION: PERSONALIZATION
 * ───────────────────────────────────────────────────────────── */
static void sec_personalization(uint32_t* fb, int W, int H, int cx, int cy, int cw, int ch) {
    (void)ch;
    sec_header(fb, W, H, cx, cy, "Personalization");
    int yy = cy + 36;

    /* Wallpaper card */
    card(fb, W, H, cx + 4, yy, cw - 8, 178, "Wallpaper");
    yy += 36;
    if (g_wp_count == 0) {
        lbl(fb, W, H, cx + 16, yy, "No wallpapers found in /home/user/wallpaper", ui_theme.dim);
        lbl(fb, W, H, cx + 16, yy + 18, "Copy .bmp images into that folder, then reopen Settings.", ui_theme.faint);
    } else {
        int per_row = (cw - 24) / 72;
        if (per_row < 1) per_row = 1;
        if (per_row > 5) per_row = 5;
        for (int i = 0; i < g_wp_count; i++) {
            int col = i % per_row, row = i / per_row;
            int bx = cx + 16 + col * 72;
            int by = yy + row * 56;
            if (by + SW_H > yy + 178 - 28) break;   /* stay inside the card */
            wp_thumb_ensure(i);
            eigen_draw_fillrect(fb, W, H, bx, by, SW_W, SW_H, 0x05080C);
            wp_thumb_blit(fb, W, H, i, bx + 2, by + 2);
            uint32_t bcol = (g_wp_sel == i) ? ui_theme.accent : ui_theme.border;
            eigen_draw_rect(fb, W, H, bx, by, SW_W, SW_H, bcol);
            if (g_wp_sel == i)
                eigen_draw_rect(fb, W, H, bx - 1, by - 1, SW_W + 2, SW_H + 2, ui_theme.accent);
            char nm[9];
            int nn = 0;
            while (g_wp[i].name[nn] && nn < 8) { nm[nn] = g_wp[i].name[nn]; nn++; }
            nm[nn] = 0;
            lbl(fb, W, H, bx, by + SW_H + 3, nm, ui_theme.faint);
            if (ui_button(&ui, bx, by, SW_W, SW_H, "")) {
                eigen_settings(EIGEN_SETTINGS_WALL_APPLY, (uint64_t)i, 0, 0);
                wp_refresh();
            }
        }
    }
    yy += 190;

    /* Accent colours card */
    card(fb, W, H, cx + 4, yy, cw - 8, 90, "Accent Colour");
    yy += 36;
    lbl(fb, W, H, cx + 16, yy, "System accent:", ui_theme.dim);
    for (int i = 0; i < ACCENT_N; i++) {
        int ax = cx + 16 + i * 42;
        eigen_draw_fillrect(fb, W, H, ax, yy + 18, 34, 34, g_accents[i]);
        eigen_draw_rect(fb, W, H, ax, yy + 18, 34, 34,
                        (g_accent_sel == i) ? 0xFFFFFF : ui_theme.border);
        if (ui_button(&ui, ax, yy + 18, 34, 34, "")) {
            g_accent_sel = i;
            eigen_settings(EIGEN_SETTINGS_ACCENT_SET, (uint64_t)i, 0, 0);
        }
    }
}

/* ─────────────────────────────────────────────────────────────
 *  SECTION: THEMES
 * ───────────────────────────────────────────────────────────── */
static int g_theme_dark = 1;
static void sec_themes(uint32_t* fb, int W, int H, int cx, int cy, int cw, int ch) {
    (void)ch;
    sec_header(fb, W, H, cx, cy, "Themes");
    int yy = cy + 36;

    card(fb, W, H, cx + 4, yy, cw - 8, 100, "Appearance Mode");
    yy += 34;
    lbl(fb, W, H, cx + 16, yy, "Choose between light and dark interface:", ui_theme.dim);
    yy += 24;
    if (ui_button(&ui, cx + 16, yy, 110, 30, "Dark Mode")) {
        g_theme_dark = 1;
        eigen_settings(0, 1, 0, 0);
    }
    if (ui_button(&ui, cx + 136, yy, 110, 30, "Light Mode")) {
        g_theme_dark = 0;
        eigen_settings(0, 0, 0, 0);
    }
    /* indicator */
    int ind_x = g_theme_dark ? cx + 16 : cx + 136;
    eigen_draw_rect(fb, W, H, ind_x - 1, yy - 1, 112, 32, ui_theme.accent);
    yy += 46;

    /* Built-in palette preview */
    yy = cy + 36 + 110;
    card(fb, W, H, cx + 4, yy, cw - 8, 130, "Palette Preview");
    uint32_t palette[] = {
        ui_theme.bg, ui_theme.panel, ui_theme.panel2, ui_theme.border,
        ui_theme.text, ui_theme.dim, ui_theme.accent, ui_theme.good,
        ui_theme.bad, ui_theme.gold
    };
    const char* pal_names[] = { "bg", "panel", "panel2", "border",
                                 "text", "dim", "accent", "good", "bad", "gold" };
    for (int i = 0; i < 10; i++) {
        int px = cx + 16 + (i % 5) * 80;
        int py = yy + 32 + (i / 5) * 44;
        eigen_draw_fillrect(fb, W, H, px, py, 70, 28, palette[i]);
        eigen_draw_rect(fb, W, H, px, py, 70, 28, ui_theme.border);
        lbl(fb, W, H, px + 2, py + 30, pal_names[i], ui_theme.dim);
    }
}

/* ─────────────────────────────────────────────────────────────
 *  SECTION: ABOUT
 * ───────────────────────────────────────────────────────────── */
static void sec_about(uint32_t* fb, int W, int H, int cx, int cy, int cw, int ch) {
    (void)ch;
    sec_header(fb, W, H, cx, cy, "About");
    int yy = cy + 36;

    card(fb, W, H, cx + 4, yy, cw - 8, 200, "System Information");
    yy += 34;

    struct eigen_sysinfo si;
    memset(&si, 0, sizeof(si));
    eigen_sysinfo(&si);

    struct {
        const char* key;
        char val[48];
    } rows[7];

    rows[0].key = "OS";
    strcpy(rows[0].val, "EigenOS x86_64");

    rows[1].key = "Version";
    strcpy(rows[1].val, "2.0 Userland");

    rows[2].key = "API Version";
    snprintf(rows[2].val, sizeof(rows[2].val), "ABI v%u", si.api_version);

    rows[3].key = "RAM";
    snprintf(rows[3].val, sizeof(rows[3].val), "%u MB", si.total_mem_kb / 1024);

    rows[4].key = "Display";
    snprintf(rows[4].val, sizeof(rows[4].val), "%ux%u px", si.screen_w, si.screen_h);

    rows[5].key = "Active Tasks";
    snprintf(rows[5].val, sizeof(rows[5].val), "%u tasks", si.task_count);

    rows[6].key = "Uptime";
    snprintf(rows[6].val, sizeof(rows[6].val), "%u s", (uint32_t)(si.uptime_ms / 1000));

    for (int i = 0; i < 7; i++) {
        int ry = yy + i * 22;
        lbl(fb, W, H, cx + 16, ry, rows[i].key, ui_theme.dim);
        lbl(fb, W, H, cx + 110, ry, rows[i].val, ui_theme.text);
    }
    yy += 7 * 22 + 8;

    lbl(fb, W, H, cx + 16, yy, "Settings v2.0 - EigenOS  |  GNU GPL v3", ui_theme.faint);
}

/* ─────────────────────────────────────────────────────────────
 *  SECTION: BLUETOOTH & PORTS
 * ───────────────────────────────────────────────────────────── */
static void sec_bluetooth_ports(uint32_t* fb, int W, int H, int cx, int cy, int cw, int ch) {
    (void)ch;
    sec_header(fb, W, H, cx, cy, "Bluetooth, USB & Serial Ports");
    int yy = cy + 36;

    card(fb, W, H, cx + 4, yy, cw - 8, 80, "Bluetooth Controller");
    lbl(fb, W, H, cx + 16, yy + 34, "Realtek / Generic Bluetooth Adapter", ui_theme.text);
    lbl(fb, W, H, cx + 16, yy + 54, "Status: Online  |  Mode: Discoverable  |  MAC: 00:1A:7D:DA:71:04", ui_theme.dim);
    yy += 90;

    card(fb, W, H, cx + 4, yy, cw - 8, 80, "USB Root Hub & Controllers");
    lbl(fb, W, H, cx + 16, yy + 34, "xHCI / UHCI USB Host Controller  (4 Root Ports)", ui_theme.text);
    lbl(fb, W, H, cx + 16, yy + 54, "Port 1: USB Mouse  |  Port 2: USB Keyboard  |  Port 3-4: Available", ui_theme.dim);
    yy += 90;

    card(fb, W, H, cx + 4, yy, cw - 8, 80, "Serial COM & Communication Ports");
    lbl(fb, W, H, cx + 16, yy + 34, "COM1 (0x3F8, IRQ 4)  |  COM2 (0x2F8, IRQ 3)", ui_theme.text);
    lbl(fb, W, H, cx + 16, yy + 54, "Baud: 115200 8N1  |  Status: Ready", ui_theme.dim);
}

/* ─────────────────────────────────────────────────────────────
 *  SECTION: SCREEN SAVER
 * ───────────────────────────────────────────────────────────── */
static void sec_screensaver(uint32_t* fb, int W, int H, int cx, int cy, int cw, int ch) {
    (void)ch;
    sec_header(fb, W, H, cx, cy, "Screen Saver");
    int yy = cy + 36;

    card(fb, W, H, cx + 4, yy, cw - 8, 110, "Screen Saver Effect");
    const char* fx[] = { "Starfield", "Matrix", "Plasma", "Orbit", "Aurora" };
    for (int i = 0; i < 5; i++) {
        int bx = cx + 16 + (i % 3) * 130;
        int by = yy + 32 + (i / 3) * 36;
        if (ui_button(&ui, bx, by, 120, 28, fx[i])) g_saver_fx = i;
        if (g_saver_fx == i)
            eigen_draw_rect(fb, W, H, bx - 1, by - 1, 122, 30, ui_theme.accent);
    }
    yy += 120;

    card(fb, W, H, cx + 4, yy, cw - 8, 80, "Idle Delay");
    const char* delays[] = { "Off", "1 min", "5 min", "15 min" };
    for (int d = 0; d < 4; d++) {
        int dx = cx + 16 + d * 110;
        if (ui_button(&ui, dx, yy + 32, 100, 28, delays[d])) g_saver_delay = d;
        if (g_saver_delay == d)
            eigen_draw_rect(fb, W, H, dx - 1, yy + 31, 102, 30, ui_theme.accent);
    }
    yy += 90;

    if (ui_button(&ui, cx + 16, yy, 180, 32, "Preview Screen Saver")) {
        /* preview trigger placeholder */
    }
}

/* ─────────────────────────────────────────────────────────────
 *  SECTION: COMING SOON
 * ───────────────────────────────────────────────────────────── */
static void sec_soon(uint32_t* fb, int W, int H, int cx, int cy, int cw, int ch,
                     const char* title, const char* desc) {
    (void)ch;
    sec_header(fb, W, H, cx, cy, title);
    int yy = cy + 50;
    card(fb, W, H, cx + 4, yy, cw - 8, 100, "Coming Soon");
    lbl(fb, W, H, cx + 16, yy + 40, desc, ui_theme.dim);
    lbl(fb, W, H, cx + 16, yy + 62, "This section is planned for a future release.", ui_theme.faint);
}

/* ─────────────────────────────────────────────────────────────
 *  SIDEBAR
 * ───────────────────────────────────────────────────────────── */
static const char* SIDEBAR_ITEMS[SEC_COUNT] = {
    "Personalization", "Themes", "About", "Bluetooth & Ports",
    "Screen Saver", "Mouse", "Taskbar", "Animations", "Shortcuts"
};
static const vector_icon_id_t SIDEBAR_ICONS[SEC_COUNT] = {
    ICON_IMAGE, ICON_PALETTE, ICON_INFO, ICON_SETTINGS,
    ICON_PLAY, ICON_MOUSE, ICON_GRID, ICON_PLAY, ICON_KEYBOARD
};

static void draw_sidebar(uint32_t* fb, int W, int H, int x, int y, int sw, int sh) {
    /* sidebar background */
    eigen_draw_fillrect(fb, W, H, x, y, sw, sh, ui_theme.panel);
    vline(fb, W, H, x + sw - 1, y, sh, ui_theme.border);

    int ry = y + 8;
    for (int i = 0; i < SEC_COUNT; i++) {
        /* "SOON" divider before Mouse */
        if (i == 5) {
            eigen_draw_text(fb, W, H, x + 12, ry, "SYSTEM", ui_theme.faint);
            ry += 18;
        }
        int ih = 34;
        int sel = (cur_sec == i);
        int hov = ui.mx >= x && ui.mx < x + sw && ui.my >= ry && ui.my < ry + ih;

        uint32_t bg = sel ? ui_theme.panel2 : (hov ? ui_theme.panel2 : ui_theme.panel);
        eigen_draw_fillrect(fb, W, H, x, ry, sw - 1, ih, bg);
        if (sel) {
            /* accent left strip */
            eigen_draw_fillrect(fb, W, H, x, ry + 6, 3, ih - 12, ui_theme.accent);
        }

        uint32_t ic = sel ? ui_theme.accent : (hov ? ui_theme.text : ui_theme.dim);
        draw_vector_icon(fb, W, H, x + 10, ry + 9, 16, SIDEBAR_ICONS[i], ic);
        eigen_draw_text(fb, W, H, x + 32, ry + 9, SIDEBAR_ITEMS[i],
                        sel ? ui_theme.accent : ui_theme.text);

        if (ui.click && hov) cur_sec = i;
        ry += ih;
    }
}

/* ─────────────────────────────────────────────────────────────
 *  HEADER / FOOTER
 * ───────────────────────────────────────────────────────────── */
static void draw_header(uint32_t* fb, int W, int H) {
    eigen_draw_fillrect(fb, W, H, 0, 0, W, HEADER_H, ui_theme.panel);
    hline(fb, W, H, 0, HEADER_H - 1, W, ui_theme.border);
    eigen_draw_fillrect(fb, W, H, 0, 0, 4, HEADER_H, ui_theme.accent);
    draw_vector_icon(fb, W, H, 14, 12, 18, ICON_SETTINGS, ui_theme.accent);
    eigen_draw_text(fb, W, H, 38, 12, "Settings", ui_theme.text);

    /* clock */
    int t[6];
    eigen_time_get(t);
    char tbuf[12];
    tbuf[0] = (char)('0' + t[0] / 10); tbuf[1] = (char)('0' + t[0] % 10);
    tbuf[2] = ':';
    tbuf[3] = (char)('0' + t[1] / 10); tbuf[4] = (char)('0' + t[1] % 10);
    tbuf[5] = ':';
    tbuf[6] = (char)('0' + t[2] / 10); tbuf[7] = (char)('0' + t[2] % 10);
    tbuf[8] = 0;
    eigen_draw_text(fb, W, H, W - 80, 12, tbuf, ui_theme.accent);
}

static void draw_footer(uint32_t* fb, int W, int H) {
    int fy = H - FOOTER_H;
    eigen_draw_fillrect(fb, W, H, 0, fy, W, FOOTER_H, ui_theme.panel);
    hline(fb, W, H, 0, fy, W, ui_theme.border);
    eigen_draw_text(fb, W, H, 14, fy + 4,
                    "Arrows: switch   Esc: close", ui_theme.faint);
}

/* ─────────────────────────────────────────────────────────────
 *  MAIN
 * ───────────────────────────────────────────────────────────── */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    eigen_printf("[settings] starting (ring-3 EigenUI)\n");

    int win = eigen_win_create(60, 40, WIN_W, WIN_H, "Settings");
    if (win < 0) { eigen_printf("[settings] window create failed\n"); return 1; }

    ui_sync_theme();
    wp_refresh();
    accent_refresh();

    int last_sec = -1;
    for (;;) {
        eigen_ev_t evs[MAX_EVS];
        int n = eigen_win_poll(win, evs, MAX_EVS);

        for (int i = 0; i < n; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) goto done;
            if (evs[i].type == EIGEN_EV_KEY) {
                if (evs[i].a >= 0x100 || (evs[i].a & 0x100)) continue;
                int k = evs[i].a & 0xFF;
                switch (k) {
                case 27: goto done;
                /* arrow keys */
                case 130: case 132: cur_sec = (cur_sec + SEC_COUNT - 1) % SEC_COUNT; break;
                case 131: case 133: cur_sec = (cur_sec + 1) % SEC_COUNT; break;
                default: break;
                }
            }
        }

        uint32_t fbw = 0, fbh = 0;
        eigen_win_getsize(win, &fbw, &fbh);
        uint32_t* fb = (uint32_t*)eigen_win_map(win);
        if (!fb) { eigen_sleep_ms(16); continue; }

        int W = (int)fbw, H = (int)fbh;

        ui_begin(&ui, fb, W, H);
        ui_feed(&ui, evs, n);

        /* ── background ── */
        eigen_draw_fillrect(fb, W, H, 0, 0, W, H, ui_theme.bg);

        /* ── header ── */
        draw_header(fb, W, H);

        /* ── sidebar ── */
        int sb_y = HEADER_H;
        int sb_h = H - HEADER_H - FOOTER_H;
        draw_sidebar(fb, W, H, 0, sb_y, SIDEBAR_W, sb_h);

        /* ── content area ── */
        int cx = SIDEBAR_W + 8;
        int cy = HEADER_H + 8;
        int cw = W - SIDEBAR_W - 16;
        int ch = H - HEADER_H - FOOTER_H - 16;

        switch (cur_sec) {
        case SEC_PERSONALIZATION:
            if (last_sec != SEC_PERSONALIZATION) { wp_refresh(); accent_refresh(); }
            sec_personalization(fb, W, H, cx, cy, cw, ch); break;
        case SEC_THEMES:
            sec_themes(fb, W, H, cx, cy, cw, ch); break;
        case SEC_ABOUT:
            sec_about(fb, W, H, cx, cy, cw, ch); break;
        case SEC_BLUETOOTH_PORTS:
            sec_bluetooth_ports(fb, W, H, cx, cy, cw, ch); break;
        case SEC_SCREENSAVER:
            sec_screensaver(fb, W, H, cx, cy, cw, ch); break;
        case SEC_MOUSE:
            sec_soon(fb, W, H, cx, cy, cw, ch, "Mouse",
                     "Pointer speed, acceleration and button mapping."); break;
        case SEC_TASKBAR:
            sec_soon(fb, W, H, cx, cy, cw, ch, "Taskbar",
                     "Position, autohide, opacity and pinned apps."); break;
        case SEC_ANIMATIONS:
            sec_soon(fb, W, H, cx, cy, cw, ch, "Animations",
                     "Window effects, motion blur and transparency."); break;
        default:
            sec_soon(fb, W, H, cx, cy, cw, ch, "Shortcuts",
                     "Customize global keyboard shortcuts."); break;
        }

        /* ── footer ── */
        draw_footer(fb, W, H);
        last_sec = cur_sec;

        ui_end(&ui);
        eigen_win_flush(win);
        eigen_sleep_ms(16);
    }

done:
    eigen_win_close(win);
    eigen_printf("[settings] bye\n");
    return 0;
}
