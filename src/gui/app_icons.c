/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* ========================================================================
 * Eigen OS — Premium Backgroundless Icon Engine
 *
 * Design principle:
 *   - NO opaque backgrounds — icons render transparently over wallpaper
 *   - Soft 2-pixel drop shadow via alpha-blended offset
 *   - RGBA bitmap icons: rendered at full 24px cell (transparent pixels skipped)
 *   - Procedural icons: crisp vector art using gfx primitives
 *   - Palette: primarily white, light-grey, and accent colours
 * ======================================================================== */

#include "gui/app_icons.h"
#include "gui/gui.h"
#include "gui/doom_icon_data.h"
#include "gui/prof_icons.h"
#include "drivers/video/gfx.h"

/* ── Constants ──────────────────────────────────────────────────────────── */

/* Full cell size — icons fill the entire allocated slot                    */
#define ICN  24        /* full cell draw size                               */
#define ICNX 0        /* x offset into cell                                 */
#define ICNY 0        /* y offset into cell                                 */

/* ── Soft drop-shadow helper ────────────────────────────────────────────── */
/* Paints a blurred, feathered dark shadow 1-2px below and to the right.    */
/* Call this BEFORE drawing the icon foreground.                            */

static void icon_shadow(int x, int y, int w, int h, int r) {
    gfx_fill_rect_alpha(x + 1, y + 2, w, h, 0x000000, 35);
    (void)r;
}

/* Round-shadow for circular icons */
static void icon_shadow_circle(int cx, int cy, int r) {
    gfx_fill_circle(cx + 2, cy + 3, r, 0x000000);
    gfx_blend_pixel(cx + 2, cy + 3, 0x000000, 80);
    gfx_fill_rect_alpha(cx - r + 1, cy - r + 2, r * 2, r * 2, 0x000000, 40);
}

/* ── RGBA bitmap icon helper ─────────────────────────────────────────────── */
/* Renders RGBA4444 data with full transparency, filling the 24px cell.     */
static void icon_rgba(int x, int y, const uint32_t* data) {
    /* Drop shadow behind the glyph */
    gfx_fill_rect_alpha(x + 2, y + 2, ICN, ICN, 0x000000, 40);
    gfx_fill_rect_alpha(x + 1, y + 1, ICN, ICN, 0x000000, 20);
    /* Render bitmap at full cell size */
    gfx_draw_icon_rgba(x, y, ICN, data);
}

/* ── CALCULATOR ─────────────────────────────────────────────────────────── */
static void draw_icon_calc(int x, int y) {
    /* Body: dark rounded rect                                               */
    icon_shadow(x+1, y+1, 20, 20, 5);
    gfx_fill_rect_rounded(x+1, y+1, 20, 20, 5, 0x1C1C2E);
    gfx_draw_rect_rounded_outline(x+1, y+1, 20, 20, 5, 1, 0x4A4A6A);

    /* Display area (light) */
    gfx_fill_rect_rounded(x+3, y+3, 16, 5, 2, 0xE2E8F0);
    /* Display text "=" */
    gfx_fill_rect(x+9, y+5,  4, 1, 0x334155);
    gfx_fill_rect(x+9, y+7,  4, 1, 0x334155);

    /* Button grid: 4 cols × 3 rows */
    uint32_t btn_colors[12] = {
        0x6B7280, 0x6B7280, 0x6B7280, 0xE53E3E,  /* row 1: grey grey grey red */
        0x4A5568, 0x4A5568, 0x4A5568, 0xE53E3E,  /* row 2 */
        0x4A5568, 0x4A5568, 0x6B7280, 0x2F855A,  /* row 3 */
    };
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            int bx = x + 3 + col * 4;
            int by = y + 10 + row * 4;
            gfx_fill_rect_rounded(bx, by, 3, 3, 1, btn_colors[row*4+col]);
        }
    }
}

/* ── FILE EXPLORER ──────────────────────────────────────────────────────── */
static void draw_icon_folder(int x, int y) {
    icon_shadow(x, y+2, 22, 18, 4);

    /* Back folder body */
    gfx_fill_rect_rounded(x+1, y+8, 21, 14, 3, 0xC8973A);
    /* Tab nub */
    gfx_fill_rect_rounded(x+1, y+6, 9, 5, 2, 0xC8973A);

    /* Front folder face — warm gold gradient simulation */
    gfx_fill_rect_rounded(x+1, y+9, 21, 13, 3, 0xF6C24A);
    gfx_fill_rect_alpha(x+2, y+9, 20, 3, 0xFFFFFF, 40); /* shine */

    /* Folder lines for depth */
    gfx_draw_hline(x+1, y+12, 21, 0xD4902A);
    gfx_draw_hline(x+1, y+13, 21, 0xE8A633);

    /* Small document icon inside */
    gfx_fill_rect_rounded(x+7, y+14, 9, 7, 1, 0xFFF8E7);
    gfx_draw_hline(x+8, y+16, 7, 0xC8973A);
    gfx_draw_hline(x+8, y+18, 5, 0xC8973A);
}

/* ── THEME MANAGER ─────────────────────────────────────────── */
static void draw_icon_theme(int x, int y) {
    /* three overlapping color swatches (palette) */
    gfx_fill_rect_rounded(x+2,  y+3,  11, 11, 3, 0x3A6EA5);
    gfx_fill_rect_rounded(x+9,  y+5,  11, 11, 3, 0xE3DAC4);
    gfx_fill_rect_rounded(x+6,  y+10, 11, 11, 3, 0xE4E4E8);
    gfx_draw_rect_rounded_outline(x+2, y+3, 11, 11, 3, 1, 0x000000);
    gfx_draw_rect_rounded_outline(x+9, y+5, 11, 11, 3, 1, 0x000000);
    gfx_draw_rect_rounded_outline(x+6, y+10, 11, 11, 3, 1, 0x000000);
}

/* ── TERMINAL ───────────────────────────────────────────────────────────── */
static void draw_icon_terminal(int x, int y) {
    icon_shadow(x+1, y+1, 20, 20, 4);
    /* Dark body */
    gfx_fill_rect_rounded(x+1, y+1, 20, 20, 4, 0x0D1117);
    /* Title bar strip */
    gfx_fill_rect_rounded(x+1, y+1, 20, 5, 4, 0x21262D);
    gfx_fill_rect(x+1, y+4, 20, 2, 0x21262D); /* square off bottom of title */
    /* Window dots */
    gfx_fill_circle(x+4,  y+3, 1, 0xFF5F57);
    gfx_fill_circle(x+7,  y+3, 1, 0xFFBD2E);
    gfx_fill_circle(x+10, y+3, 1, 0x28C840);

    /* Prompt chevron ">" */
    gfx_draw_line(x+4, y+9,  x+7,  y+11, 0x00FF88);
    gfx_draw_line(x+4, y+13, x+7,  y+11, 0x00FF88);
    /* Cursor block */
    gfx_fill_rect(x+9, y+9, 2, 5, 0xE2E8F0);

    /* Two code lines */
    gfx_fill_rect_alpha(x+4, y+16, 14, 1, 0x4ADE80, 200);
    gfx_fill_rect_alpha(x+4, y+18, 9,  1, 0x4ADE80, 120);
}

/* ── ANIMATION MANAGER ───────────────────────────────────────────────────── */
static void draw_icon_anim(int x, int y) {
    icon_shadow(x+1, y+1, 20, 20, 4);
    /* glossy dark tile */
    gfx_fill_rect_rounded(x+1, y+1, 20, 20, 5, 0x141A26);
    gfx_fill_rect_rounded(x+1, y+1, 20, 12, 5, 0x1F2A3D);   /* top sheen */
    gfx_draw_rect_rounded_outline(x+1, y+1, 20, 20, 5, 1, 0x2C3A52);

    /* accent motion ring */
    uint32_t ac = get_accent_color();
    gfx_draw_circle(x+11, y+11, 9, ac);

    /* bold play triangle inside the ring */
    gfx_fill_triangle(x+8,  y+7,  x+8,  y+15, x+16, y+11, 0xF6C500);

    /* orbit sparkle dot */
    gfx_fill_circle(x+18, y+6, 1, 0xFFFFFF);
}

/* ── CALENDAR ───────────────────────────────────────────────────────────── */
static void draw_icon_calendar(int x, int y) {
    icon_shadow(x+1, y+1, 20, 21, 4);
    /* Body */
    gfx_fill_rect_rounded(x+1, y+3, 20, 19, 4, 0xF8FAFC);
    /* Header bar */
    gfx_fill_rect_rounded(x+1, y+3, 20, 7, 4, 0xE53E3E);
    gfx_fill_rect(x+1, y+7, 20, 3, 0xE53E3E); /* square off header bottom */
    /* Ring notches on header */
    gfx_fill_rect(x+5,  y+1, 2, 5, 0xC53030);
    gfx_fill_rect(x+15, y+1, 2, 5, 0xC53030);
    gfx_fill_rect_rounded(x+4,  y+1, 4, 4, 2, 0xFED7D7);
    gfx_fill_rect_rounded(x+14, y+1, 4, 4, 2, 0xFED7D7);

    /* Day number "15" in white */
    gfx_draw_string_transparent(x+6, y+11, "15", 0xE53E3E);

    /* Day grid dots (small 2x2 squares) */
    for (int col = 0; col < 5; col++) {
        gfx_fill_rect(x+3 + col*4, y+17, 2, 2, 0xCBD5E1);
    }
    gfx_fill_rect(x+3,  y+17, 2, 2, 0xCBD5E1);
    gfx_fill_rect(x+19, y+17, 2, 2, 0xE53E3E); /* highlight last */
}

/* ── BROWSER / HTTP VIEWER ──────────────────────────────────────────────── */
static void draw_icon_edrowser(int x, int y) {
    icon_shadow(x+1, y+1, 20, 20, 10);
    /* Globe sphere */
    gfx_fill_circle(x+11, y+11, 10, 0x1A56DB);
    gfx_fill_rect_alpha(x+1, y+1, 21, 10, 0xFFFFFF, 20); /* top gloss */

    /* Longitude lines (vertical) */
    gfx_draw_vline(x+11, y+1, 20, 0xFFFFFF);
    gfx_draw_vline(x+7,  y+3, 16, 0x93C5FD);
    gfx_draw_vline(x+15, y+3, 16, 0x93C5FD);

    /* Latitude lines */
    gfx_draw_hline(x+1,  y+11, 20, 0xFFFFFF);
    gfx_draw_hline(x+3,  y+7,  16, 0x93C5FD);
    gfx_draw_hline(x+3,  y+15, 16, 0x93C5FD);

    /* Outline */
    gfx_draw_circle(x+11, y+11, 10, 0x1E40AF);
}

/* ── CLOCK ──────────────────────────────────────────────────────────────── */
static void draw_icon_clock(int x, int y) {
    icon_shadow_circle(x+11, y+11, 10);
    /* Clock face */
    gfx_fill_circle(x+11, y+11, 10, 0x1A202C);
    gfx_fill_circle(x+11, y+11, 9,  0xF7FAFC);
    /* Hour ticks */
    for (int i = 0; i < 12; i++) {
        /* Use approximate positions for hour markers */
        int angles_x[12] = {11,14,16,17,16,14,11,8,6,5,6,8};
        int angles_y[12] = {3,4,7,11,15,18,19,18,15,11,7,4};
        gfx_fill_circle(x+angles_x[i], y+angles_y[i], 1, 0xA0AEC0);
    }
    /* Clock hands */
    gfx_draw_line(x+11, y+11, x+11, y+5,  0x1A202C);  /* 12 o'clock hour  */
    gfx_draw_line(x+11, y+11, x+16, y+11, 0x1A202C);  /* 3 o'clock minute */
    gfx_draw_line(x+11, y+11, x+11, y+5,  0x2D3748);
    gfx_draw_line(x+11, y+11, x+16, y+11, 0x4A5568);
    /* Centre dot */
    gfx_fill_circle(x+11, y+11, 1, 0xE53E3E);
}

/* ── PIANO / MUSIC ──────────────────────────────────────────────────────── */
static void draw_icon_piano(int x, int y) {
    icon_shadow(x+1, y+5, 21, 14, 3);
    /* White keys body */
    gfx_fill_rect_rounded(x+1, y+5, 21, 14, 3, 0xF8FAFC);
    gfx_draw_rect_rounded_outline(x+1, y+5, 21, 14, 3, 1, 0xCBD5E1);

    /* Key dividers (7 keys) */
    for (int k = 1; k < 7; k++) {
        gfx_draw_vline(x+1+k*3, y+6, 12, 0xCBD5E1);
    }

    /* Black keys (5 of them) */
    int bk_x[] = {3, 6, 9, 12, 15}; /* approximate positions */
    /* Typical piano layout: black keys at 1,2, 4,5,6 (groups of 2 and 3) */
    int black_keys[] = {x+4, x+7, x+13, x+16, x+19};
    for (int k = 0; k < 5; k++) {
        gfx_fill_rect_rounded(black_keys[k], y+5, 2, 8, 1, 0x1A202C);
    }
    (void)bk_x;
}

/* ── SNAKE GAME ─────────────────────────────────────────────────────────── */
static void draw_icon_snake(int x, int y) {
    icon_shadow(x+2, y+2, 20, 20, 4);
    /* Dark green field */
    gfx_fill_rect_rounded(x+2, y+2, 20, 20, 4, 0x166534);

    /* Snake body — segmented S-curve */
    /* Segment 1: tail (right side, bottom) */
    gfx_fill_rect_rounded(x+13, y+17, 6, 3, 1, 0x4ADE80);
    gfx_fill_rect_rounded(x+13, y+14, 3, 4, 1, 0x4ADE80);
    /* Segment 2: middle */
    gfx_fill_rect_rounded(x+5,  y+14, 9, 3, 1, 0x4ADE80);
    gfx_fill_rect_rounded(x+5,  y+11, 3, 4, 1, 0x4ADE80);
    /* Segment 3: head (left side, top) */
    gfx_fill_rect_rounded(x+5,  y+8,  8, 3, 1, 0x4ADE80);

    /* Snake head — slightly brighter */
    gfx_fill_rect_rounded(x+12, y+8, 3, 3, 1, 0x86EFAC);
    /* Eyes */
    gfx_fill_circle(x+13, y+9, 1, 0x000000);

    /* Apple dot */
    gfx_fill_circle(x+8, y+5, 2, 0xEF4444);
    gfx_fill_circle(x+9, y+4, 1, 0x22C55E); /* leaf */
}

/* ── MINESWEEPER ─────────────────────────────────────────────────────────── */
static void draw_icon_mines(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 3);
    /* Grid background */
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 3, 0xE2E8F0);

    /* Mine cells: a 5×5 grid, some revealed, some flagged */
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            int cx = x + 2 + col * 4;
            int cy = y + 2 + row * 4;
            gfx_fill_rect(cx, cy, 3, 3, 0xCBD5E1);
            gfx_draw_rect_outline(cx, cy, 3, 3, 1, 0xFFFFFF);
        }
    }

    /* Revealed cells (numbers) */
    gfx_fill_rect(x+6,  y+2, 3, 3, 0xBEE3F8); /* 1 cell */
    gfx_fill_rect(x+10, y+2, 3, 3, 0xFEBCBC); /* mine: red */
    gfx_fill_rect(x+2,  y+6, 3, 3, 0xBEE3F8);

    /* Mine symbol on red cell */
    gfx_fill_circle(x+11, y+3, 1, 0x1A202C);

    /* Flag on safe cell */
    gfx_draw_vline(x+3, y+7, 3, 0x1A202C);
    gfx_fill_rect(x+3, y+7, 2, 2, 0xEF4444);
}

/* ── TETRIS ─────────────────────────────────────────────────────────────── */
static void draw_icon_tetris(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0E1526);

    /* Tetris pieces in bright colours */
    /* L-piece: cyan */
    gfx_fill_rect_rounded(x+3,  y+15, 4, 4, 1, 0x22D3EE);
    gfx_fill_rect_rounded(x+7,  y+15, 4, 4, 1, 0x22D3EE);
    gfx_fill_rect_rounded(x+7,  y+11, 4, 4, 1, 0x22D3EE);

    /* S-piece: green */
    gfx_fill_rect_rounded(x+11, y+11, 4, 4, 1, 0x4ADE80);
    gfx_fill_rect_rounded(x+15, y+11, 4, 4, 1, 0x4ADE80);
    gfx_fill_rect_rounded(x+7,  y+7,  4, 4, 1, 0x4ADE80);
    gfx_fill_rect_rounded(x+11, y+7,  4, 4, 1, 0x4ADE80);

    /* I-piece: yellow-orange */
    gfx_fill_rect_rounded(x+3,  y+3,  4, 4, 1, 0xFBBF24);
    gfx_fill_rect_rounded(x+7,  y+3,  4, 4, 1, 0xFBBF24);
    gfx_fill_rect_rounded(x+11, y+3,  4, 4, 1, 0xFBBF24);
    gfx_fill_rect_rounded(x+15, y+3,  4, 4, 1, 0xFBBF24);

    /* T-piece partial: magenta at bottom-right */
    gfx_fill_rect_rounded(x+11, y+15, 4, 4, 1, 0xF472B6);
    gfx_fill_rect_rounded(x+15, y+15, 4, 4, 1, 0xF472B6);
    gfx_fill_rect_rounded(x+15, y+19, 4, 4, 1, 0xF472B6);

    /* Cell outlines for depth */
    gfx_fill_rect_alpha(x+3, y+3, 16, 20, 0x000000, 15);
}

/* ── PAIRS (MEMORY GAME) ─────────────────────────────────────────────────── */
static void draw_icon_pairs(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x1A1A2E);

    /* 4×4 card grid */
    uint32_t card_colors[16] = {
        0xF472B6, 0xFBBF24, 0x4ADE80, 0x60A5FA,
        0xF472B6, 0x9CA3AF, 0x9CA3AF, 0x60A5FA,
        0x9CA3AF, 0xFBBF24, 0x9CA3AF, 0xA78BFA,
        0x9CA3AF, 0x9CA3AF, 0xA78BFA, 0x4ADE80,
    };
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int cx = x + 2 + col * 5;
            int cy = y + 2 + row * 5;
            uint32_t c = card_colors[row*4+col];
            if (c == 0x9CA3AF) {
                /* Face-down card */
                gfx_fill_rect_rounded(cx, cy, 4, 4, 1, 0x374151);
                gfx_fill_rect_alpha(cx, cy, 4, 1, 0xFFFFFF, 30);
            } else {
                /* Face-up card */
                gfx_fill_rect_rounded(cx, cy, 4, 4, 1, c);
            }
        }
    }
}

/* ── 2048 ────────────────────────────────────────────────────────────────── */
static void draw_icon_2048(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0xFAF0E6);

    /* Grid lines */
    for (int i = 1; i < 4; i++) {
        gfx_draw_vline(x+1+i*5, y+2, 20, 0xBBB0A0);
        gfx_draw_hline(x+2, y+1+i*5, 20, 0xBBB0A0);
    }

    /* Tiles: */
    uint32_t tile_c[] = {0xCDC1B4, 0xEEE4DA, 0xEDE0C8, 0xF2B179};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int tx = x + 2 + c * 5;
            int ty = y + 2 + r * 5;
            int idx = (r+c)%4;
            gfx_fill_rect_rounded(tx, ty, 4, 4, 1, tile_c[idx]);
        }
    }
    /* Highlight "2048" tile */
    gfx_fill_rect_rounded(x+17, y+17, 4, 4, 1, 0xEDC53F);
}

/* ── SUDOKU ──────────────────────────────────────────────────────────────── */
static void draw_icon_sudoku(int x, int y) {
    icon_shadow(x+1, y+2, 21, 21, 3);
    /* White grid */
    gfx_fill_rect_rounded(x+1, y+2, 21, 21, 3, 0xF8FAFC);
    /* Grid lines (9×9 simplified as 3×3 blocks) */
    gfx_draw_rect_outline(x+1, y+2, 21, 21, 1, 0x94A3B8);
    /* Block dividers (thick) */
    gfx_fill_rect(x+7, y+2, 1, 21, 0x475569);
    gfx_fill_rect(x+14, y+2, 1, 21, 0x475569);
    gfx_fill_rect(x+1, y+9, 21, 1, 0x475569);
    gfx_fill_rect(x+1, y+16, 21, 1, 0x475569);
    /* Thin sub-dividers */
    gfx_draw_vline(x+4,  y+2, 21, 0xCBD5E1);
    gfx_draw_vline(x+11, y+2, 21, 0xCBD5E1);
    gfx_draw_vline(x+18, y+2, 21, 0xCBD5E1);
    gfx_draw_hline(x+1, y+5,  21, 0xCBD5E1);
    gfx_draw_hline(x+1, y+12, 21, 0xCBD5E1);
    gfx_draw_hline(x+1, y+19, 21, 0xCBD5E1);
    /* Some filled numbers (small dots to represent digits) */
    gfx_fill_rect(x+9, y+4,  2, 3, 0xE53E3E); /* highlight number */
    gfx_fill_rect(x+2, y+11, 2, 3, 0x4299E1);
    gfx_fill_rect(x+16, y+18, 2, 3, 0x48BB78);
}

/* ── FLAPPY BIRD ─────────────────────────────────────────────────────────── */
static void draw_icon_flappy(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    /* Sky gradient */
    gfx_fill_gradient_v(x+1, y+1, 22, 22, 0x38BDF8, 0x0EA5E9);

    /* Pipes */
    gfx_fill_rect(x+14, y+1,  5, 9,  0x4ADE80);
    gfx_fill_rect(x+13, y+1,  7, 3,  0x22C55E); /* pipe cap */
    gfx_fill_rect(x+14, y+15, 5, 9,  0x4ADE80);
    gfx_fill_rect(x+13, y+21, 7, 3,  0x22C55E);

    /* Bird body */
    gfx_fill_circle(x+8, y+11, 4, 0xFBBF24);
    /* Wing */
    gfx_fill_rect_rounded(x+6, y+13, 5, 2, 1, 0xF59E0B);
    /* Eye */
    gfx_fill_circle(x+10, y+10, 1, 0xFFFFFF);
    gfx_fill_circle(x+10, y+10, 0, 0x1A202C);
    /* Beak */
    gfx_fill_rect(x+12, y+11, 3, 2, 0xEF4444);
}

/* ── PIPES GAME ──────────────────────────────────────────────────────────── */
static void draw_icon_pipes(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    /* Dark board tile (matches game background) */
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0D1424);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0x33415F);

    /* Pipe elbow: vertical arm */
    gfx_fill_rect_rounded(x+4, y+3, 6, 12, 2, 0xB45309);
    gfx_fill_rect(x+5, y+3, 2, 12, 0xFBBF24);
    /* horizontal arm */
    gfx_fill_rect_rounded(x+4, y+10, 17, 6, 2, 0xB45309);
    gfx_fill_rect(x+4, y+11, 17, 2, 0xFBBF24);

    /* Junction hub */
    gfx_fill_circle(x+7, y+13, 3, 0xFDE68A);
    gfx_fill_circle(x+7, y+13, 1, 0xB45309);

    /* Flowing fluid (teal) */
    gfx_fill_circle(x+12, y+12, 2, 0x2DD4BF);
    gfx_fill_circle(x+17, y+13, 2, 0x2DD4BF);
    gfx_fill_circle(x+13, y+12, 1, 0x99F6E4);

    /* End flanges */
    gfx_fill_rect_rounded(x+3, y+2, 8, 3, 1, 0xD97706);
    gfx_draw_rect_rounded_outline(x+3, y+2, 8, 3, 1, 1, 0x78350F);
    gfx_fill_rect_rounded(x+20, y+9, 3, 8, 1, 0xD97706);
    gfx_draw_rect_rounded_outline(x+20, y+9, 3, 8, 1, 1, 0x78350F);
}

/* ── ON-SCREEN KEYBOARD ──────────────────────────────────────────────────── */
static void draw_icon_osk(int x, int y) {
    icon_shadow(x+1, y+5, 21, 15, 4);
    /* Keyboard body */
    gfx_fill_rect_rounded(x+1, y+5, 21, 15, 4, 0x2D3748);
    /* Key rows */
    /* Row 1 — 8 tiny keys */
    for (int k = 0; k < 8; k++) {
        gfx_fill_rect_rounded(x+2+k*3, y+7, 2, 3, 1, 0x4A5568);
    }
    /* Row 2 — 7 keys (staggered) */
    for (int k = 0; k < 7; k++) {
        gfx_fill_rect_rounded(x+3+k*3, y+11, 2, 3, 1, 0x4A5568);
    }
    /* Spacebar */
    gfx_fill_rect_rounded(x+5, y+15, 12, 3, 1, 0x4A5568);

    /* Accent: Enter key (green) */
    gfx_fill_rect_rounded(x+19, y+11, 2, 4, 1, 0x48BB78);
}

/* ── IMAGE VIEWER ────────────────────────────────────────────────────────── */
static void draw_icon_imgview(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    /* White card */
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0xF8FAFC);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0xCBD5E1);

    /* Sky gradient in image area */
    gfx_fill_gradient_v(x+3, y+3, 18, 13, 0x7DD3FC, 0xBFDBFE);

    /* Mountain silhouette */
    gfx_fill_rect(x+3, y+11, 18, 5, 0x166534);
    gfx_draw_line(x+3, y+14, x+9, y+9,  0x166534);
    gfx_draw_line(x+9, y+9,  x+14, y+13, 0x166534);
    gfx_draw_line(x+14, y+13, x+21, y+8, 0x166534);
    gfx_draw_line(x+21, y+8, x+21, y+16, 0x166534);
    /* Fill mountain */
    for (int dy = 0; dy < 7; dy++) {
        gfx_draw_hline(x+3, y+10+dy, 18, 0x166534);
    }

    /* Sun */
    gfx_fill_circle(x+18, y+6, 3, 0xFBBF24);

    /* Film strip below */
    gfx_fill_rect(x+2, y+17, 20, 5, 0x1A202C);
    for (int k = 0; k < 4; k++) {
        gfx_fill_rect(x+3+k*5, y+18, 3, 3, 0x374151);
    }
}

/* ── BITMAP MAKER / DRAW ─────────────────────────────────────────────────── */
static void draw_icon_bitmap_maker(int x, int y) {
    icon_shadow(x+1, y+1, 21, 21, 4);
    /* Canvas */
    gfx_fill_rect_rounded(x+1, y+1, 21, 21, 4, 0xFFFFFF);
    gfx_draw_rect_rounded_outline(x+1, y+1, 21, 21, 4, 1, 0xCBD5E1);

    /* Checkerboard pattern in corner (transparency indicator) */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            if ((r+c)%2 == 0) gfx_fill_rect(x+1+c*3, y+1+r*3, 3, 3, 0xE2E8F0);
        }
    }

    /* Colour palette swatches */
    uint32_t swatches[] = {0xEF4444, 0xF59E0B, 0x10B981, 0x3B82F6, 0x8B5CF6, 0xEC4899};
    for (int s = 0; s < 6; s++) {
        gfx_fill_rect(x+1+s*3, y+18, 3, 4, swatches[s]);
    }

    /* Brush stroke */
    gfx_draw_line_aa(x+4, y+14, x+18, y+4, 0x1A202C);
    gfx_fill_circle(x+18, y+4, 2, 0x2DD4BF);
}

/* ── PAINT STUDIO ───────────────────────────────────────────── */
static void draw_icon_paint(int x, int y) {
    icon_shadow(x+1, y+1, 21, 21, 4);
    /* easel / canvas */
    gfx_fill_rect_rounded(x+2, y+1, 19, 16, 3, 0xFFFFFF);
    gfx_draw_rect_rounded_outline(x+2, y+1, 19, 16, 3, 1, 0x334155);
    /* paint blobs */
    uint32_t cols[] = {0xEF4444, 0xF59E0B, 0x10B981, 0x3B82F6, 0x8B5CF6, 0xEC4899};
    gfx_fill_circle(x+7, y+6, 3, cols[0]);
    gfx_fill_circle(x+14, y+7, 3, cols[3]);
    gfx_fill_rect(x+9, y+11, 4, 3, cols[4]);
    /* brush */
    gfx_draw_line_aa(x+5, y+19, x+18, y+8, 0x94A3B8);
    gfx_fill_circle(x+18, y+8, 2, 0x1A202C);
    gfx_fill_rect(x+3, y+17, 4, 4, 0x8B5CF6);
}

/* ── PCI SCANNER ─────────────────────────────────────────────────────────── */
static void draw_icon_pci(int x, int y) {
    icon_shadow(x+1, y+4, 21, 15, 3);
    /* PCB board */
    gfx_fill_rect_rounded(x+1, y+4, 21, 15, 3, 0x064E3B);
    /* Traces */
    gfx_draw_hline(x+3, y+8,  15, 0x34D399);
    gfx_draw_hline(x+3, y+12, 15, 0x34D399);
    gfx_draw_vline(x+6, y+6,  10, 0x34D399);
    gfx_draw_vline(x+12, y+6, 10, 0x34D399);
    gfx_draw_vline(x+17, y+6, 10, 0x34D399);

    /* IC chip */
    gfx_fill_rect_rounded(x+8, y+6, 8, 8, 2, 0x111827);
    gfx_draw_rect_rounded_outline(x+8, y+6, 8, 8, 2, 1, 0x6EE7B7);

    /* Chip pins */
    for (int p = 0; p < 3; p++) {
        gfx_fill_rect(x+9+p*2, y+5, 1, 2, 0xA7F3D0);
        gfx_fill_rect(x+9+p*2, y+13, 1, 2, 0xA7F3D0);
    }

    /* PCI slot connector tabs */
    for (int t = 0; t < 5; t++) {
        gfx_fill_rect(x+2+t*3, y+18, 2, 4, 0x065F46);
    }
}

/* ── PERSONALIZATION ─────────────────────────────────────────────────────── */
/* ── SETTINGS (GEAR) ─────────────────────────────────────────────────────── */
static void draw_icon_settings(int x, int y) {
    icon_shadow_circle(x+11, y+11, 10);
    /* 8 gear teeth (fixed integer positions, no float math) */
    static const int teeth[8][2] = {
        { 11,  3 }, { 18,  5 }, { 20, 11 }, { 18, 17 },
        { 11, 19 }, {  4, 17 }, {  2, 11 }, {  4,  5 }
    };
    for (int t = 0; t < 8; t++)
        gfx_fill_circle(x + teeth[t][0], y + teeth[t][1], 3, 0x64748B);
    gfx_fill_circle(x+11, y+11, 9, 0x94A3B8);
    gfx_fill_circle(x+11, y+11, 5, 0xE2E8F0);
    gfx_fill_circle(x+11, y+11, 2, 0x3B82F6);
}

/* ── TASKBAR LAYOUT MANAGER ─────────────────────────────── */
static void draw_icon_layout(int x, int y) {    /* overlapping stacked bars suggesting a configurable taskbar */
    icon_shadow(x+2, y+2, 20, 20, 5);
    gfx_fill_rect(x+2,  y+2,  20, 4, 0x4DA6FF);
    gfx_fill_rect(x+2,  y+8,  20, 3, 0x7EE787);
    gfx_fill_rect(x+5,  y+13, 14, 3, 0xD4D4D8);
    gfx_fill_rect(x+9,  y+17, 10, 3, 0xBF5AF2);
    gfx_draw_rect_outline(x+2, y+2, 20, 20, 1, 0xE2E8F0);
}

static void draw_icon_personalization(int x, int y) {
    /* Colour wheel + settings cog hybrid */
    icon_shadow_circle(x+11, y+11, 10);
    /* Colour wheel segments */
    uint32_t wheel_cols[] = {0xFF2D55, 0xFF9F0A, 0x30D158, 0x00C7BE, 0x0A84FF, 0xBF5AF2};
    int wx[] = {11, 15, 17, 14, 7, 5};
    int wy[] = {4,  7,  12, 17, 16, 10};
    for (int s = 0; s < 6; s++) {
        gfx_fill_circle(x+wx[s], y+wy[s], 5, wheel_cols[s]);
    }
    /* Centre overlay */
    gfx_fill_circle(x+11, y+11, 4, 0xF8FAFC);
    /* Paint brush handle in centre */
    gfx_fill_rect(x+10, y+8,  2, 6, 0x92400E);
    gfx_fill_circle(x+11, y+14, 2, 0x2DD4BF);
    gfx_draw_circle(x+11, y+11, 10, 0xE2E8F0);
}

/* ── SYSTEM INFO ─────────────────────────────────────────────────────────── */
static void draw_icon_system(int x, int y) {
    /* CPU chip icon */
    icon_shadow(x+3, y+3, 18, 18, 3);
    gfx_fill_rect_rounded(x+3, y+3, 18, 18, 3, 0x1E293B);
    gfx_draw_rect_rounded_outline(x+3, y+3, 18, 18, 3, 1, 0x475569);

    /* Inner die */
    gfx_fill_rect_rounded(x+6, y+6, 12, 12, 2, 0x334155);
    gfx_draw_rect_rounded_outline(x+6, y+6, 12, 12, 2, 1, 0x60A5FA);

    /* Pin rows (4 on each side) */
    for (int p = 0; p < 4; p++) {
        gfx_fill_rect(x+6+p*3, y+1, 2, 3, 0x94A3B8);   /* top */
        gfx_fill_rect(x+6+p*3, y+20, 2, 3, 0x94A3B8);  /* bottom */
        gfx_fill_rect(x+1, y+6+p*3, 3, 2, 0x94A3B8);   /* left */
        gfx_fill_rect(x+20, y+6+p*3, 3, 2, 0x94A3B8);  /* right */
    }

    /* CPU core grid (2×2 cores) */
    gfx_fill_rect_rounded(x+7,  y+7,  4, 4, 1, 0x3B82F6);
    gfx_fill_rect_rounded(x+13, y+7,  4, 4, 1, 0x3B82F6);
    gfx_fill_rect_rounded(x+7,  y+13, 4, 4, 1, 0x3B82F6);
    gfx_fill_rect_rounded(x+13, y+13, 4, 4, 1, 0x3B82F6);
}

/* ── KERNEL LOG ──────────────────────────────────────────────────────────── */
static void draw_icon_kernellog(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0D1117);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0x22C55E);

    /* Log lines — varying lengths and alphas */
    gfx_fill_rect_alpha(x+3, y+4,  16, 2, 0x22C55E, 255);
    gfx_fill_rect_alpha(x+3, y+7,  12, 2, 0x4ADE80, 200);
    gfx_fill_rect_alpha(x+3, y+10, 18, 2, 0x22C55E, 255);
    gfx_fill_rect_alpha(x+3, y+13, 8,  2, 0x16A34A, 160);
    gfx_fill_rect_alpha(x+3, y+16, 14, 2, 0x4ADE80, 200);
    gfx_fill_rect_alpha(x+3, y+19, 11, 2, 0x22C55E, 180);

    /* Cursor blink block */
    gfx_fill_rect(x+17, y+19, 3, 2, 0x4ADE80);
}

/* ── NETWORK DEBUG ───────────────────────────────────────────────────────── */
static void draw_icon_netdebug(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0F172A);

    /* Network rings (concentric arcs) */
    gfx_draw_circle(x+11, y+20, 5,  0x3B82F6);
    gfx_draw_circle(x+11, y+20, 9,  0x2563EB);
    gfx_draw_circle(x+11, y+20, 13, 0x1D4ED8);
    /* Erase bottom half to show only top arcs */
    gfx_fill_rect(x+1, y+20, 22, 5, 0x0F172A);

    /* Node dots */
    gfx_fill_circle(x+11, y+11, 2, 0x60A5FA);
    gfx_fill_circle(x+5,  y+17, 2, 0x93C5FD);
    gfx_fill_circle(x+17, y+17, 2, 0x93C5FD);

    /* Connection lines */
    gfx_draw_line(x+11, y+11, x+5,  y+17, 0x3B82F6);
    gfx_draw_line(x+11, y+11, x+17, y+17, 0x3B82F6);
    gfx_draw_line(x+5,  y+17, x+17, y+17, 0x3B82F6);

    /* Data packet dots */
    gfx_fill_circle(x+8,  y+14, 1, 0xFBBF24);
    gfx_fill_circle(x+14, y+14, 1, 0xFBBF24);
}

/* ── GRAPHING CALCULATOR ─────────────────────────────────────────────────── */
static void draw_icon_graphing(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0F172A);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0x1E3A5F);

    /* Axes */
    gfx_draw_hline(x+2, y+12, 20, 0x4A5568);
    gfx_draw_vline(x+12, y+2, 20, 0x4A5568);

    /* Sine curve in cyan */
    int sy[] = {12,10, 7, 6, 7,10,12,14,17,18,17,14,12,10, 7, 6, 7,10,12,14};
    for (int i = 0; i < 19; i++) {
        gfx_blend_pixel(x+3+i, y+sy[i], 0x22D3EE, 240);
        if (i > 0) gfx_draw_line(x+2+i, y+sy[i-1], x+3+i, y+sy[i], 0x22D3EE);
    }

    /* A second function in magenta */
    int fy[] = {5,6,8,11,14,17,19,20,19,17,14,11,8,6,5,6,8,11,14,17};
    for (int i = 1; i < 19; i++) {
        gfx_draw_line(x+2+i, y+fy[i-1], x+3+i, y+fy[i], 0xF472B6);
    }
}

/* ── HEX VIEWER / DUMP ───────────────────────────────────────────────────── */
static void draw_icon_hexdump(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0D1117);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0x00E5FF);

    /* Hex code lines — pairs of two "digits" (2×3 rects) */
    uint32_t hex_cols[] = {0x00E5FF, 0x67E8F9, 0x22D3EE, 0x00E5FF};
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int hx = x+3 + col*5;
            int hy = y+4 + row*5;
            gfx_fill_rect_rounded(hx, hy, 3, 3, 1, hex_cols[(row+col)%4]);
        }
    }

    /* Separator line */
    gfx_draw_vline(x+12, y+3, 18, 0x1E3A5F);

    /* ASCII representation */
    gfx_fill_rect_alpha(x+13, y+4,  8, 1, 0x94A3B8, 200);
    gfx_fill_rect_alpha(x+13, y+7,  6, 1, 0x94A3B8, 200);
    gfx_fill_rect_alpha(x+13, y+10, 8, 1, 0x94A3B8, 200);
    gfx_fill_rect_alpha(x+13, y+13, 4, 1, 0x94A3B8, 200);
    gfx_fill_rect_alpha(x+13, y+16, 7, 1, 0x94A3B8, 200);
    gfx_fill_rect_alpha(x+13, y+19, 5, 1, 0x94A3B8, 200);
}

/* ── PROCESS VIEWER ──────────────────────────────────────────────────────── */
static void draw_icon_process(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x111827);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0x374151);

    /* Process list rows */
    uint32_t row_cols[] = {0x3B82F6, 0x10B981, 0xEF4444, 0xF59E0B, 0x8B5CF6};
    for (int r = 0; r < 5; r++) {
        int ry = y + 3 + r * 4;
        /* PID dot */
        gfx_fill_circle(x+4, ry+1, 1, row_cols[r]);
        /* Process name bar */
        gfx_fill_rect_rounded(x+7, ry, 8, 3, 1, 0x374151);
        /* CPU bar */
        int bar_w = 2 + (r * 2) % 6;
        gfx_fill_rect_rounded(x+16, ry, bar_w, 3, 1, row_cols[r]);
    }
}

/* ── MANDELBROT ──────────────────────────────────────────────────────────── */
static void draw_icon_mandelbrot(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x030010);

    /* Approximate mandelbrot-like colourful pattern */
    uint32_t frac_cols[] = {
        0xFF6B6B, 0xFF9F43, 0xFECA57, 0x48DBFB,
        0xFF9FF4, 0x54A0FF, 0x00D2D3, 0x5F27CD,
    };
    /* Radial arcs from center */
    for (int ring = 0; ring < 8; ring++) {
        int r = 2 + ring;
        gfx_fill_circle(x+11, y+11, r, frac_cols[ring % 8]);
    }
    /* Dark cutout core */
    gfx_fill_circle(x+11, y+11, 5, 0x030010);
    /* Inner fractal details */
    gfx_fill_circle(x+11, y+11, 2, 0x5F27CD);
    /* Off-centre bulb */
    gfx_fill_circle(x+8, y+11, 3, 0x030010);
}

/* ── GLGears (three interlocking metallic gears) ───────────────────────────────── */
static void draw_icon_glgears(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    /* premium dark tile */
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 5, 0x0B0E16);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 5, 1, 0x243049);
    gfx_fill_rect_alpha(x+2, y+2, 20, 7, 0xFFFFFF, 14); /* top gloss */

    /* 12-direction tooth offset table (r=6, no trig) */
    static const int tx[12] = {6,5,3,0,-3,-5,-6,-5,-3,0,3,5};
    static const int ty[12] = {0,3,5,6,5,3,0,-3,-5,-6,-5,-3};

    uint32_t cols[3]   = {0xE5483B, 0x36C46A, 0x3E7BE0};
    uint32_t dk[3]     = {0x9E2A20, 0x1F8A47, 0x275AB0};
    int cx[3] = {9, 16, 11};
    int cy[3] = {11, 9, 17};

    for (int i = 0; i < 3; i++) {
        int gx = x + 1 + cx[i], gy = y + 1 + cy[i];
        /* teeth */
        for (int t = 0; t < 12; t++)
            gfx_fill_rect_rounded(gx + tx[t] - 1, gy + ty[t] - 1, 3, 3, 1, cols[i]);
        /* body */
        gfx_fill_circle(gx, gy, 5, cols[i]);
        /* inner shade ring */
        gfx_draw_circle(gx, gy, 4, dk[i]);
        /* hub */
        gfx_fill_circle(gx, gy, 3, dk[i]);
        /* center hole */
        gfx_fill_circle(gx, gy, 1, 0x0B0E16);
        /* gloss highlight */
        gfx_fill_circle(gx - 2, gy - 2, 1, 0xFFFFFF);
    }
}

/* ── GLCube (glossy shaded isometric cube) ─────────────────────────────── */
static void draw_icon_glcube(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 5, 0x0B0E16);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 5, 1, 0x243049);
    gfx_fill_rect_alpha(x+2, y+2, 20, 7, 0xFFFFFF, 14); /* top gloss */

    int cx0 = x + 12, cy0 = y + 12;   /* cube centre */
    int s = 8;                        /* half-width at the equator */
    int h = 4;                        /* vertical lift of the top face */
    uint32_t ac = get_accent_color();
    /* top face (lightest, glossy) */
    gfx_fill_triangle(cx0 - s, cy0 - h, cx0,      cy0 - s, cx0 + s, cy0 - h, 0x6FB8FF);
    gfx_fill_triangle(cx0 - s, cy0 - h, cx0 + s, cy0 - h, cx0,      cy0 - s, 0x6FB8FF);
    gfx_fill_rect_alpha(cx0 - s + 1, cy0 - h, s, (s-h)/2, 0xFFFFFF, 36); /* top sheen */
    /* left face (mid) */
    gfx_fill_triangle(cx0 - s, cy0 - h, cx0,      cy0 - s, cx0,      cy0 + s, 0x2E6FB0);
    gfx_fill_triangle(cx0 - s, cy0 - h, cx0,      cy0 + s, cx0 - s, cy0 + h, 0x2E6FB0);
    /* right face (darkest) */
    gfx_fill_triangle(cx0 + s, cy0 - h, cx0,      cy0 - s, cx0,      cy0 + s, 0x1E5A96);
    gfx_fill_triangle(cx0 + s, cy0 - h, cx0,      cy0 + s, cx0 + s, cy0 + h, 0x1E5A96);
    /* accent edge highlight (front vertical) */
    gfx_draw_line(cx0, cy0 - s, cx0, cy0 + s, ac);
}

/* ── GLTeapot (glossy red classic teapot) ──────────────────────────────── */
static void draw_icon_glteapot(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 5, 0x0B0E16);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 5, 1, 0x243049);
    gfx_fill_rect_alpha(x+2, y+2, 20, 7, 0xFFFFFF, 14); /* top gloss */

    int cxp = x + 11, cyp = y + 15;      /* body centre */
    uint32_t body = 0xE5484F, dk = 0xB23039, hi = 0xFF9C8C;
    /* spout (left) */
    gfx_fill_triangle(cxp - 9, cyp - 5, cxp - 11, cyp + 1, cxp - 2, cyp - 6, body);
    gfx_fill_triangle(cxp - 9, cyp - 5, cxp - 2, cyp - 6, cxp - 3, cyp - 2, dk);
    /* handle (right) */
    gfx_draw_circle(cxp + 8, cyp - 3, 5, body);
    gfx_fill_circle(cxp + 8, cyp - 3, 5, body);
    gfx_fill_circle(cxp + 8, cyp - 3, 3, 0x0B0E16);
    /* body */
    gfx_fill_circle(cxp, cyp, 7, body);
    gfx_draw_circle(cxp, cyp, 6, dk);
    /* lid */
    gfx_fill_rect_rounded(x + 7, y + 5, 9, 3, 1, dk);
    gfx_fill_circle(cxp, y + 6, 3, body);
    gfx_fill_circle(cxp, y + 5, 1, hi);
    /* gloss highlight on body */
    gfx_fill_circle(cxp - 3, cyp - 3, 2, hi);
    gfx_fill_circle(cxp - 4, cyp + 1, 1, 0xFFFFFF);
}

/* ── JULIA SET ───────────────────────────────────────────────────────────── */
static void draw_icon_julia(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x04001A);

    /* Swirling fractal pattern */
    uint32_t j_cols[] = {0xBF5AF2, 0x9D4EDD, 0x7B2FBE, 0xFF2D55, 0xFF6B9D};
    for (int s = 0; s < 5; s++) {
        gfx_fill_circle(x+11, y+11, 10-s*1, j_cols[s]);
    }
    /* Spiral texture via arcs */
    gfx_fill_rect_alpha(x+3, y+3, 10, 10, 0xBF5AF2, 40);
    gfx_fill_rect_alpha(x+11, y+11, 10, 10, 0xFF2D55, 40);
    gfx_fill_circle(x+11, y+11, 3, 0x04001A);
    gfx_fill_circle(x+7,  y+7,  2, 0xFF6B9D);
    gfx_fill_circle(x+15, y+15, 2, 0xBF5AF2);
}

/* ── EYES ────────────────────────────────────────────────────────────────── */
static void draw_icon_eyes(int x, int y) {
    icon_shadow(x+1, y+5, 22, 14, 6);
    gfx_fill_rect_rounded(x+1, y+5, 22, 14, 6, 0x1E1B4B);

    /* Left eye */
    gfx_fill_circle(x+7,  y+12, 5, 0xF8FAFC);
    gfx_fill_circle(x+7,  y+12, 3, 0x1E1B4B);
    gfx_fill_circle(x+7,  y+12, 2, 0x3730A3);
    gfx_fill_circle(x+8,  y+11, 1, 0xFFFFFF);  /* specular */

    /* Right eye */
    gfx_fill_circle(x+17, y+12, 5, 0xF8FAFC);
    gfx_fill_circle(x+17, y+12, 3, 0x1E1B4B);
    gfx_fill_circle(x+17, y+12, 2, 0x3730A3);
    gfx_fill_circle(x+18, y+11, 1, 0xFFFFFF);
}

/* ── EDIM ────────────────────────────────────────────────────────────────── */
static void draw_icon_edim(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 5);
    /* Dark circle */
    gfx_fill_circle(x+11, y+11, 11, 0x1A0030);
    /* Bright ring */
    gfx_draw_circle(x+11, y+11, 10, 0x7C3AED);
    gfx_draw_circle(x+11, y+11, 9,  0xA855F7);

    /* "B" letterform in violet */
    /* Vertical stem */
    gfx_fill_rect(x+7, y+5, 2, 12, 0xC084FC);
    /* Top bump */
    gfx_fill_rect_rounded(x+9, y+5, 4, 5, 2, 0xC084FC);
    /* Bottom bump (slightly wider) */
    gfx_fill_rect_rounded(x+9, y+10, 5, 6, 2, 0xC084FC);

    /* Glow dot */
    gfx_fill_circle(x+11, y+11, 2, 0xE879F9);
}

/* ── GAMES (GAMEPAD) ─────────────────────────────────────────────────────── */
static void draw_icon_games(int x, int y) {
    icon_shadow(x+2, y+6, 20, 12, 5);

    /* Controller body — dual-lobe shape */
    gfx_fill_rect_rounded(x+2, y+8, 20, 10, 4, 0x1E293B);
    gfx_fill_circle(x+6,  y+11, 5, 0x1E293B);  /* left grip */
    gfx_fill_circle(x+18, y+11, 5, 0x1E293B);  /* right grip */

    /* D-pad cross */
    gfx_fill_rect(x+5,  y+10, 4, 2, 0x475569);  /* horizontal */
    gfx_fill_rect(x+6,  y+9,  2, 4, 0x475569);  /* vertical */

    /* Action buttons (ABXY style) */
    gfx_fill_circle(x+18, y+10, 2, 0xEF4444);   /* A */
    gfx_fill_circle(x+15, y+8,  2, 0xFBBF24);   /* B/X */
    gfx_fill_circle(x+21, y+8,  2, 0x4ADE80);   /* Y */
    gfx_fill_circle(x+18, y+6,  2, 0x60A5FA);   /* X */

    /* Joystick nubs */
    gfx_fill_circle(x+8,  y+14, 2, 0x334155);
    gfx_fill_circle(x+16, y+14, 2, 0x334155);
    gfx_fill_circle(x+8,  y+14, 1, 0x64748B);
    gfx_fill_circle(x+16, y+14, 1, 0x64748B);

    /* Start/select tiny buttons */
    gfx_fill_rect(x+10, y+10, 2, 1, 0x475569);
    gfx_fill_rect(x+13, y+10, 2, 1, 0x475569);
}

/* ── COLOUR WHEEL ────────────────────────────────────────────────────────── */
static void draw_icon_colour(int x, int y) {
    /* Colour wheel — true 6-segment disc with white centre */
    icon_shadow_circle(x+11, y+11, 10);
    gfx_fill_circle(x+11, y+11, 10, 0xFFFFFF);

    /* 6 hue segments approximated via overlapping circles */
    gfx_fill_circle(x+11, y+5,  6, 0xFF2D55);   /* red    12 o'clock */
    gfx_fill_circle(x+16, y+8,  6, 0xFF9F0A);   /* orange 2 o'clock  */
    gfx_fill_circle(x+16, y+15, 6, 0x30D158);   /* green  4 o'clock  */
    gfx_fill_circle(x+11, y+17, 6, 0x00C7BE);   /* cyan   6 o'clock  */
    gfx_fill_circle(x+6,  y+15, 6, 0x0A84FF);   /* blue   8 o'clock  */
    gfx_fill_circle(x+6,  y+8,  6, 0xBF5AF2);   /* purple 10 o'clock */

    /* White centre */
    gfx_fill_circle(x+11, y+11, 4, 0xFFFFFF);
    /* Outer outline */
    gfx_draw_circle(x+11, y+11, 10, 0xE2E8F0);
    /* Dropper tool icon in centre */
    gfx_fill_circle(x+11, y+11, 2, 0x1A202C);
}

/* ── SHUTDOWN / POWER ────────────────────────────────────────────────────── */
static void draw_icon_shutdown(int x, int y) {
    icon_shadow_circle(x+11, y+11, 10);
    gfx_fill_circle(x+11, y+11, 10, 0x1A202C);
    gfx_draw_circle(x+11, y+11, 10, 0x374151);

    /* Power arc (¾ circle) — simulate with large circle minus bottom */
    gfx_draw_circle(x+11, y+11, 7,  0xEF4444);
    /* Erase bottom part of arc */
    gfx_fill_rect(x+4, y+14, 14, 8, 0x1A202C);

    /* Re-draw outline at bottom to restore circle edge */
    gfx_draw_circle(x+11, y+11, 10, 0x374151);

    /* Power bar */
    gfx_fill_rect(x+10, y+3, 2, 10, 0xEF4444);
    gfx_fill_rect(x+10, y+3, 2, 4,  0xF87171); /* lighter top */

    /* Glow aura */
    gfx_blend_pixel(x+11, y+4,  0xEF4444, 80);
    gfx_blend_pixel(x+10, y+4,  0xEF4444, 60);
    gfx_blend_pixel(x+12, y+4,  0xEF4444, 60);
}

/* ── ACCESSIBILITY ───────────────────────────────────────────────────────── */
static void draw_icon_accessibility(int x, int y) {
    /* Stick figure, white with teal accent */
    icon_shadow_circle(x+11, y+11, 10);
    gfx_fill_circle(x+11, y+11, 10, 0x134E4A);
    gfx_draw_circle(x+11, y+11, 10, 0x2DD4BF);

    /* Head */
    gfx_fill_circle(x+11, y+6, 2, 0xF0FDFA);
    /* Body */
    gfx_draw_vline(x+11, y+8, 6, 0xF0FDFA);
    /* Arms — raised */
    gfx_draw_line(x+11, y+9, x+7,  y+7, 0xF0FDFA);
    gfx_draw_line(x+11, y+9, x+15, y+7, 0xF0FDFA);
    /* Legs */
    gfx_draw_line(x+11, y+14, x+8,  y+19, 0xF0FDFA);
    gfx_draw_line(x+11, y+14, x+14, y+19, 0xF0FDFA);
}

/* ── DEBUG ───────────────────────────────────────────────────────────────── */
static void draw_icon_debug(int x, int y) {
    icon_shadow(x+5, y+4, 14, 16, 4);

    /* Bug body — rounded oval */
    gfx_fill_rect_rounded(x+6, y+8, 12, 11, 5, 0xEF4444);
    gfx_fill_rect_alpha(x+7, y+8, 10, 3, 0xFFFFFF, 40); /* shine */

    /* Bug head */
    gfx_fill_circle(x+12, y+7, 4, 0xEF4444);
    gfx_fill_circle(x+12, y+7, 3, 0xFCA5A5);

    /* Eyes */
    gfx_fill_circle(x+10, y+6, 1, 0x1A202C);
    gfx_fill_circle(x+14, y+6, 1, 0x1A202C);

    /* Antennae */
    gfx_draw_line_aa(x+10, y+4, x+7, y+2, 0xFCA5A5);
    gfx_draw_line_aa(x+14, y+4, x+17, y+2, 0xFCA5A5);
    gfx_fill_circle(x+7,  y+2, 1, 0xFBBF24);
    gfx_fill_circle(x+17, y+2, 1, 0xFBBF24);

    /* Segment lines */
    gfx_draw_hline(x+6, y+12, 12, 0xDC2626);
    gfx_draw_hline(x+6, y+15, 12, 0xDC2626);

    /* Legs */
    gfx_draw_line(x+6, y+10, x+3, y+9,  0xEF4444);
    gfx_draw_line(x+6, y+13, x+3, y+13, 0xEF4444);
    gfx_draw_line(x+6, y+16, x+3, y+17, 0xEF4444);
    gfx_draw_line(x+18, y+10, x+21, y+9,  0xEF4444);
    gfx_draw_line(x+18, y+13, x+21, y+13, 0xEF4444);
    gfx_draw_line(x+18, y+16, x+21, y+17, 0xEF4444);
}

/* ── ABOUT ────────────────────────────────────────────────────────────────── */
static void draw_icon_about(int x, int y) {
    icon_shadow(x, y+1, 22, 22, 6);
    gfx_fill_circle(x+11, y+11, 11, 0xC9D1D9);
    gfx_draw_circle(x+11, y+11, 11, 0xE6EDF3);

    /* "i" glyph */
    gfx_fill_circle(x+11, y+6, 2, 0x0D1117);
    gfx_fill_rect_rounded(x+9, y+11, 5, 8, 2, 0x0D1117);
}

/* ── CHECKERS GAME ────────────────────────────────────────────────────────── */
static void draw_icon_checkers(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    /* Dark board tile (matches game background) */
    gfx_fill_rect_rounded(x+1, y+1, 22, 22, 4, 0x0D1424);
    gfx_draw_rect_rounded_outline(x+1, y+1, 22, 22, 4, 1, 0x33415F);

    /* 4x4 checkerboard */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            gfx_fill_rect(x+3 + c*5, y+3 + r*5, 5, 5,
                          ((r + c) & 1) ? 0x44577E : 0x23304C);
        }
    }

    /* Discs on dark squares: RED (0,1)+(2,1), GOLD (1,0)+(3,0) */
    gfx_fill_circle(x+10, y+5, 2, 0xF43F5E);
    gfx_draw_circle(x+10, y+5, 2, 0xFF8FA3);
    gfx_fill_circle(x+10, y+15, 2, 0xF43F5E);
    gfx_draw_circle(x+10, y+15, 2, 0xFF8FA3);
    gfx_fill_circle(x+5, y+10, 2, 0xF5B53D);
    gfx_draw_circle(x+5, y+10, 2, 0xFFE9A8);
    gfx_fill_circle(x+15, y+20, 2, 0xF5B53D);
    gfx_draw_circle(x+15, y+20, 2, 0xFFE9A8);
}

/* ── WALLPAPER ──────────────────────────────────────────────────────────── */
static void draw_icon_wallpaper(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect_rounded(x+2, y+2, 20, 20, 4, 0x232B45);
    gfx_draw_rect_rounded_outline(x+2, y+2, 20, 20, 4, 1, 0x4A5A80);
    gfx_fill_rect(x+4, y+4, 16, 10, 0x3E6FB4);        /* sky */
    gfx_fill_rect(x+4, y+14, 16, 6, 0x2E5E44);        /* ground */
    gfx_fill_circle(x+15, y+7, 3, 0xF5D76E);          /* sun */
    gfx_fill_rect(x+6, y+12, 4, 8, 0x5A6A8E);         /* mountain */
    gfx_fill_rect(x+13, y+10, 5, 10, 0x3E4E6E);
}

/* ── DOOM ───────────────────────────────────────────────────────────────────── */
static void draw_icon_doom(int x, int y) {
    /* DoomGuy face, extracted from the official DOOM shareware IWAD
       (STFST01 status-bar graphic, palette-mapped through PLAYPAL). */
    icon_rgba(x, y, doom_icon_data);
}

/* ── KILO EDITOR ────────────────────────────────────────────────────────── */
static void draw_icon_kilo(int x, int y) {
    icon_shadow(x+1, y+1, 20, 20, 5);
    /* Document body: dark panel */
    gfx_fill_rect_rounded(x+1, y+1, 20, 20, 3, 0x1A1B26);
    gfx_draw_rect_rounded_outline(x+1, y+1, 20, 20, 3, 1, 0x4A4A6A);

    /* Folded corner hint */
    gfx_fill_rect(x+15, y+4, 5, 1, 0x7AA2F7);

    /* Text lines (code-ish: keyword, string, number, comment) */
    gfx_fill_rect(x+4,  y+6,  9, 1, 0xFF9E64);   /* keyword  */
    gfx_fill_rect(x+4,  y+9,  6, 1, 0x9ECE6A);   /* string   */
    gfx_fill_rect(x+11, y+9,  4, 1, 0x9ECE6A);
    gfx_fill_rect(x+4,  y+12, 7, 1, 0xE0AF68);   /* number   */
    gfx_fill_rect(x+4,  y+15, 12, 1, 0x565F89);  /* comment  */

    /* Cursor block on the active line */
    gfx_fill_rect(x+12, y+12, 1, 2, 0x7AA2F7);
}

/* ── FALLBACK ─────────────────────────────────────────────────────────────── */
static void draw_icon_fallback(int x, int y) {    icon_shadow(x+2, y+2, 20, 20, 5);
    gfx_fill_circle(x+12, y+12, 10, 0x374151);
    gfx_draw_circle(x+12, y+12, 10, 0x4A5568);
    gfx_fill_rect_alpha(x+2, y+2, 20, 10, 0xFFFFFF, 15);

    /* "?" glyph */
    gfx_fill_rect_rounded(x+8, y+6, 8, 2, 1, 0xE2E8F0);
    gfx_fill_rect_alpha(x+9, y+7, 6, 3, 0x374151, 255); /* cutout */
    gfx_fill_rect(x+14, y+7, 2, 5, 0xE2E8F0);
    gfx_fill_rect_rounded(x+9, y+11, 5, 3, 1, 0xE2E8F0);
    gfx_fill_circle(x+12, y+18, 2, 0xE2E8F0);
}

/* ========================================================================
 * Dispatch
 * ======================================================================== */

static void draw_icon_ring3(int x, int y) {
    icon_shadow(x+1, y+1, 22, 22, 4);
    gfx_fill_rect(x+3, y+3, 18, 18, 0x1E1E22);
    gfx_draw_rect_outline(x+3, y+3, 18, 18, 1, 0x2A2A2E);
    gfx_fill_rect(x+5, y+5, 14, 14, 0x3FB950);
    gfx_fill_rect(x+8, y+8, 8, 8, 0x0D0E12);
    gfx_draw_string_transparent(x + 7, y + 9, "R3", 0x3FB950);
}

/* ── DEMO REEL ─────────────────────────────────────────────────────────── */
static void draw_icon_demo(int x, int y) {
    icon_shadow(x+1, y+2, 21, 17, 4);
    /* filmstrip body */
    gfx_fill_rect_rounded(x+1, y+6, 21, 13, 3, 0x1F2937);
    /* sprocket holes */
    for (int i = 0; i < 4; i++) {
        gfx_fill_circle(x+6+i*5, y+9, 3, 0x0F172A);
        gfx_fill_circle(x+6+i*5, y+18, 3, 0x0F172A);
    }
    /* a little play triangle in the centre */
    gfx_fill_triangle(x+11, y+9, x+17, y+13, x+11, y+17, 0x38BDF2);
}
/* ── BLUETOOTH ─────────────────────────────────────────────────────────── */
static void draw_icon_bluetooth(int x, int y) {
    icon_shadow(x, y+2, 22, 16, 4);
    gfx_fill_rect_rounded(x+1, y+6, 21, 13, 3, 0x373080);   /* deep purple */
    gfx_draw_rect_rounded_outline(x+1, y+6, 21, 13, 3, 1, 0x8B5CF6);
    /* classic bluetooth "B" wings (two triangles sharing centre) */
    int cx = x+11, cy = y+12;
    gfx_fill_triangle(cx-4, cy+1, cx+4, cy-4, cx+4, cy+0, 0xA78BFA);
    gfx_fill_triangle(cx-4, cy+1, cx+4, cy+4, cx+4, cy+0, 0xA78BFA);
    gfx_draw_string_transparent(x+8, y+9, "B", 0xFFFFFF);
}
/* ── SCREEN SAVER ──────────────────────────────────────────────────────── */
static void draw_icon_saver(int x, int y) {
    icon_shadow_circle(x+11, y+11, 10);
    /* moon (dark cutout over a glow disk) */
    gfx_fill_circle(x+11, y+11, 10, 0xC7D2FE);
    /* crescent bite */
    gfx_fill_rect(x+7, y+4, 9, 8, 0x090B0F);
    /* stars */
    gfx_fill_circle(x+4, y+4, 1, 0xF1F5F9);
    gfx_fill_circle(x+19, y+6, 1, 0xF1F5F9);
    gfx_fill_circle(x+6, y+19, 1, 0xF1F5F9);
}
/* ── PONG ARCADE ───────────────────────────────────────────────────────── */
static void draw_icon_pong(int x, int y) {
    icon_shadow(x, y+3, 22, 16, 3);
    /* playing field */
    gfx_fill_rect_rounded(x+1, y+6, 21, 13, 2, 0x0F172A);
    /* centre dashed line */
    for (int i = 0; i < 6; i++)
        gfx_fill_rect(x+10, y+8+i*3, 2, 1, 0x334155);
    /* paddles */
    gfx_fill_rect(x+3, y+7, 3, 10, 0xE2E8F0);
    gfx_fill_rect(x+17, y+9, 3, 10, 0xE2E8F0);
    /* ball */
    gfx_fill_circle(x+11, y+12, 2, 0xFACC15);
}
/* ── SPACE INVADERS ────────────────────────────────────────────────────── */
static void draw_icon_space(int x, int y) {
    icon_shadow(x, y+4, 22, 14, 3);
    /* body (rounded U shape) */
    gfx_fill_rect_rounded(x+2, y+6, 19, 9, 2, 0x4ADE80);
    /* head/antenna */
    gfx_fill_triangle(x+9, y+5, x+11, y+2, x+13, y+5, 0x4ADE80);
    /* eyes */
    gfx_fill_circle(x+7,  y+9, 2, 0x0F172A);
    gfx_fill_circle(x+15, y+9, 2, 0x0F172A);
    /* cannon */
    gfx_fill_triangle(x+8, y+14, x+10, y+16, x+12, y+14, 0x4ADE80);
}

/* ── MOUSE ──────────────────────────────────────────── */
static void draw_icon_mouse(int x, int y) {
    icon_shadow(x+1, y+3, 20, 16, 3);
    /* cursor body (arrow) */
    gfx_fill_triangle(x+3,  y+4,  x+16, y+4,  x+3,  y+17, 0xE2E8F0);
    gfx_fill_triangle(x+3,  y+4,  x+16, y+4,  x+3,  y+17, 0xE2E8F0);
    /* outline feel */
    gfx_fill_triangle(x+3,  y+4,  x+13, y+7,  x+3,  y+14, 0x0F172A);
    /* neon dot to hint "themes" */
    gfx_fill_circle(x+8, y+9, 2, 0x22D3EE);
}

/* ── WEATHER (modern: layered sun + soft cloud, iOS-style) ───────────────────────────────── */
static void draw_icon_weather(int x, int y) {
    icon_shadow(x + 2, y + 4, 20, 17, 4);

    /* Sun peeking from behind the cloud — warm amber with highlight */
    int sx = x + 15, sy = y + 7;
    gfx_fill_circle(sx, sy, 6, 0xFBBF24);
    gfx_fill_circle(sx - 1, sy - 1, 3, 0xFDE68A);
    gfx_fill_circle(sx + 2, sy + 2, 1, 0xFDE68A);

    /* Cloud — three stacked puffs with a soft underside */
    uint32_t cloud     = 0xF1F5F9;
    uint32_t cloud_hi  = 0xFFFFFF;
    gfx_fill_circle(x + 8,  y + 11, 4, cloud);
    gfx_fill_circle(x + 13, y + 10, 5, cloud);
    gfx_fill_circle(x + 17, y + 12, 3, cloud);
    gfx_fill_rect_rounded(x + 5, y + 11, 14, 6, 3, cloud);
    /* top highlight */
    gfx_fill_circle(x + 8,  y + 10, 3, cloud_hi);
    gfx_fill_circle(x + 13, y + 9, 4, cloud_hi);
    /* underside shading */
    gfx_fill_rect_alpha(x + 6, y + 15, 12, 1, 0x64748B, 60);
}

void draw_app_icon(const char* name, int x, int y) {
    if (!name || name[0] == '-') return;

    /* Prefix checks for ambiguous names */
    if (name[0] == 'H' && name[1] == 'T') {    /* legacy label */
        draw_icon_edrowser(x, y); return;
    }
    if (name[0] == 'B' && name[1] == 'M' && name[2] == 'P') { /* BMP Viewer window title */
        draw_icon_imgview(x, y); return;
    }
    if (name[0] == 'G' && name[6] == 'n') { draw_icon_graphing(x, y);   return; }
    if (name[0] == 'G' && name[5] == 0)   { draw_icon_graphing(x, y);   return; }
    if (name[0] == 'E' && name[1] == 'D') { draw_icon_edim(x, y);       return; }
    if (name[0] == 'A' && name[1] == 'b') { draw_icon_about(x, y);      return; }

    switch (name[0]) {
        case 'A':
            if      (name[1] == 'c') draw_icon_accessibility(x, y);
            else if (name[1] == 'p') draw_icon_folder(x, y);
            else if (name[1] == 'n') draw_icon_anim(x, y);
            else                     draw_icon_fallback(x, y);
            break;
        case 'B':
            if      (name[1] == 'i') draw_icon_bitmap_maker(x, y);
            else if (name[1] == 'l') draw_icon_bluetooth(x, y);
            else                     draw_icon_fallback(x, y);
            break;
        case 'C':
            if      (name[1] == 'a') { if (name[3] == 'e') draw_icon_calendar(x, y); else draw_icon_calc(x, y); }
            else if (name[1] == 'l') draw_icon_clock(x, y);
            else if (name[1] == 'u') draw_icon_colour(x, y);
            else if (name[1] == 'o') draw_icon_colour(x, y);
            else if (name[1] == 'h') draw_icon_checkers(x, y);
            else                     draw_icon_fallback(x, y);
            break;
        case 'D':
            if      (name[1] == 'e' && name[2] == 'm') draw_icon_demo(x, y); /* Demo Reel */
            else if (name[1] == 'e' && name[2] == 'b') draw_icon_debug(x, y);
            else if (name[1] == 'O') draw_icon_doom(x, y);   /* DOOM */
            else                                   draw_icon_fallback(x, y);
            break;
        case 'E': draw_icon_eyes(x, y); break;
        case 'F':
            if      (name[1] == 'i') draw_icon_folder(x, y);
            else if (name[1] == 'l') draw_icon_flappy(x, y);
            else                     draw_icon_fallback(x, y);
            break;
        case 'G':
            if      (name[1] == 'a') draw_icon_games(x, y);
            else if (name[1] == 'r' && name[6] == 'c') draw_icon_imgview(x, y);
            else if (name[1] == 'L') {
                if      (name[2] == 'C') draw_icon_glcube(x, y);   /* GLCube (legacy) */
                else if (name[3] == 'D') draw_icon_glcube(x, y);   /* GL Demos */
                else if (name[2] == 'T') draw_icon_glteapot(x, y); /* GLTeapot */
                else                     draw_icon_glgears(x, y);  /* GLGears */
            }
            else                     draw_icon_fallback(x, y);
            break;
        case 'H': draw_icon_hexdump(x, y); break;
        case 'I': draw_icon_imgview(x, y); break;
        case 'J': draw_icon_julia(x, y);   break;
        case 'K':
            if      (name[1] == 'e') draw_icon_kernellog(x, y);
            else if (name[1] == 'i') draw_icon_kilo(x, y);   /* Kilo window title */
            else                     draw_icon_fallback(x, y);
            break;
        case 'L':
            if (name[1] == 'a') draw_icon_layout(x, y);   /* Layout */
            else                draw_icon_fallback(x, y);
            break;
        case 'M':
            if      (name[1] == 'a') draw_icon_mandelbrot(x, y);
            else if (name[1] == 'i') draw_icon_mines(x, y);
            else if (name[1] == 'o') draw_icon_mouse(x, y);  /* Mouse Settings */
            else                     draw_icon_fallback(x, y);
            break;
        case 'N':
            draw_icon_netdebug(x, y);
            break;
        case 'O': draw_icon_osk(x, y);       break;
        case 'P':
            if      (name[1] == 'C') draw_icon_pci(x, y);
            else if (name[1] == 'a' && name[4] == 't') draw_icon_paint(x, y); /* Paint Studio */
            else if (name[1] == 'a') draw_icon_pairs(x, y);
            else if (name[1] == 'e') draw_icon_personalization(x, y);
            else if (name[1] == 'i') {
                if (name[2] == 'a') draw_icon_piano(x, y);
                else                draw_icon_pipes(x, y);
            }
            else if (name[1] == 'o' && name[2] == 'n') draw_icon_pong(x, y);  /* Pong Arcade */
            else if (name[1] == 'o')                   draw_icon_shutdown(x, y);
            else if (name[1] == 'r') draw_icon_process(x, y);
            else                     draw_icon_fallback(x, y);
            break;
        case 'R': draw_icon_ring3(x, y); break;
        case 'S':
            if      (name[1] == 'c') draw_icon_saver(x, y);    /* Screen Saver */
            else if (name[1] == 'n') draw_icon_snake(x, y);
            else if (name[1] == 'p' && name[2] == 'a') draw_icon_space(x, y);  /* Space Invaders */
            else if (name[1] == 'p')                   draw_icon_games(x, y);
            else if (name[1] == 'u') draw_icon_sudoku(x, y);
            else if (name[1] == 'y') draw_icon_system(x, y);
            else if (name[1] == 'e') draw_icon_settings(x, y); /* Settings */
            else                     draw_icon_fallback(x, y);
            break;
        case 'T':
            if (name[1] == 'h') draw_icon_theme(x, y);
            else if (name[1] == 'e') {
                if      (name[2] == 'r') draw_icon_terminal(x, y);
                else if (name[2] == 't') draw_icon_tetris(x, y);
                else if (name[2] == 'x') draw_icon_kilo(x, y);   /* Text Editor (Kilo) */
                else                     draw_icon_fallback(x, y);
            } else                       draw_icon_fallback(x, y);
            break;
        case 'W':
            if (name[1]=='e' && name[2]=='a') draw_icon_weather(x, y); /* Weather */
            else                            draw_icon_wallpaper(x, y);
            break;
        case '2': draw_icon_2048(x, y); break;
        default:  draw_icon_fallback(x, y);
    }
}

/* ── Start Button ────────────────────────────────────────────────────────── */

void draw_start_icon(int x, int y, int w, int h) {
    uint32_t c      = get_accent_color();
    uint32_t c_lite = gfx_lighten(c, 50);
    uint32_t c_dark = gfx_darken(c, 40);
    int cx = x + w / 2;
    int cy = y + h / 2;

    /* Four-pointed star */
    for (int arm = 0; arm < 4; arm++) {
        int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
        if (arm == 0) { dx1=0; dy1=-10; dx2=0; dy2=-5; }
        if (arm == 1) { dx1=10; dy1=0;  dx2=5;  dy2=0; }
        if (arm == 2) { dx1=0; dy1=10;  dx2=0;  dy2=5; }
        if (arm == 3) { dx1=-10; dy1=0; dx2=-5; dy2=0; }
        gfx_draw_line(cx, cy, cx+dx1, cy+dy1, c_dark);
        gfx_draw_line(cx, cy, cx+dx2, cy+dy2, c);
        gfx_draw_line(cx, cy, cx+(dx2/2), cy+(dy2/2), c_lite);
    }
    gfx_draw_line(cx-1, cy-1, cx-6, cy-6, c);
    gfx_draw_line(cx+1, cy-1, cx+6, cy-6, c);
    gfx_draw_line(cx+1, cy+1, cx+6, cy+6, c);
    gfx_draw_line(cx-1, cy+1, cx-6, cy+6, c);
    gfx_fill_circle(cx, cy, 3, c_lite);
    gfx_fill_circle(cx, cy, 2, 0xFFFFFF);
}

/* ── Taskbar Search ──────────────────────────────────────────────────────── */

void draw_search_icon(int x, int y, int w, int h) {
    int cx = x + w / 2 - 2;
    int cy = y + h / 2 - 2;
    uint32_t c = 0x94A3B8;

    gfx_draw_circle(cx, cy, 5, c);
    gfx_draw_circle(cx, cy, 4, c);
    gfx_draw_line(cx+3, cy+3, cx+7, cy+7, c);
    gfx_draw_line(cx+4, cy+3, cx+8, cy+7, c);
    gfx_fill_circle(cx+7, cy+6, 2, c);
}

/* ── Icon Tile ─────────────────────────────────────────────────────────────
 * Draws a rounded icon tile of `size` px (square) with the 24px glyph
 * centered inside. Gives icons a larger, modern, tile-like appearance
 * without rescaling the glyph art. `state`: 0 normal, 1 hover, 2 selected.
 * `acc` is the live accent colour (used for hover/selection ring). */
void draw_app_icon_tile(const char* name, int x, int y, int size, int state, uint32_t acc) {
    if (!name || name[0] == '-') return;

    /* Nordzy themed icon first; procedural glyph is the fallback */
    extern int icon_theme_draw(const char* app, int x, int y, int size);
    if (icon_theme_draw(name, x, y, size)) {
        if (state == 2) {
            gfx_draw_rect_outline(x, y, size, size, 2, acc);
        } else if (state == 1) {
            gfx_fill_rect_alpha(x, y, size, size, 0xFFFFFF, 26);
        }
        return;
    }

    if (state == 2) {
        gfx_fill_rect_rounded(x, y, size, size, 10, acc);
        gfx_fill_rect_alpha(x, y, size, size, acc, 50);
    } else if (state == 1) {
        gfx_fill_rect_rounded(x, y, size, size, 10, 0x202430);
        gfx_fill_rect_alpha(x, y, size, size, 0xFFFFFF, 30);
        gfx_draw_rect_rounded_outline(x, y, size, size, 10, 1, 0x454C5E);
    }

    /* center the 24px glyph */
    int gx = x + (size - 24) / 2;
    int gy = y + (size - 24) / 2;
    draw_app_icon(name, gx, gy);
}

/* macOS-style circular icon tile: soft drop shadow + round badge + glyph.
 * `state`: 0=normal, 1=hover, 2=active/accent. */
void draw_app_icon_tile_circular(const char* name, int x, int y, int size, int state, uint32_t acc) {
    if (!name || name[0] == '-') return;
    int cx = x + size / 2, cy = y + size / 2, r = size / 2;

    /* soft drop shadow (offset down-right) */
    gfx_fill_circle(cx + 1, cy + 2, r, 0x000000);
    gfx_blend_pixel(cx + 1, cy + 2, 0x000000, 90);
    gfx_fill_rect_alpha(x + 2, y + r, size - 4, r, 0x000000, 30);

    /* round badge background */
    uint32_t badge = (state == 2) ? acc
                   : (state == 1) ? 0x232A39
                                   : 0x1A1F2B;
    gfx_fill_circle(cx, cy, r, badge);
    gfx_draw_circle(cx, cy, r, (state == 2) ? gfx_lighten(acc, 45) : 0x3A4150);
    if (state == 1) {  /* hover ring highlight */
        gfx_draw_circle(cx, cy, r - 1, 0x4A5468);
    }

    /* center the 24px glyph */
    draw_app_icon(name, cx - 12, cy - 12);
}
