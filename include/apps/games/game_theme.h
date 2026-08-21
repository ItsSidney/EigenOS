/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen OS — Shared Game Theme
//  Common neon/glossy drawing helpers so every game shares one
//  visual language. No blur/3D available; everything is drawn
//  with gradients, rounded rects, alpha and glow blobs.
// ============================================================
#ifndef GAME_THEME_H
#define GAME_THEME_H

#include "drivers/video/gfx.h"
#include "gui/gui.h"   // get_accent_color
#include <stdint.h>

// ── Neon panel (rounded, vertical gradient + bright top edge) ──
static inline void gt_panel(int x, int y, int w, int h, int radius,
                            uint32_t top, uint32_t bot) {
    gfx_fill_gradient_v_rounded(x, y, w, h, radius, top, bot);
    gfx_draw_rect_rounded_outline(x, y, w, h, radius, 1, gfx_lighten(top, 24));
}

// ── Glossy button (gradient body, outline, hover/active states) ──
static inline void gt_button(int x, int y, int w, int h, uint32_t base,
                             int hovered, int active) {
    uint32_t top = active ? gfx_darken(base, 30) : gfx_lighten(base, 22);
    uint32_t bot = active ? gfx_darken(base, 55) : base;
    gfx_fill_gradient_v_rounded(x, y, w, h, h / 2, top, bot);
    uint32_t ocol = hovered ? get_accent_color() : gfx_lighten(base, 40);
    gfx_draw_rect_rounded_outline(x, y, w, h, h / 2, 1, ocol);
}

// ── Soft glow blob (concentric circles, outer dim → inner bright) ──
static inline void gt_glow(int cx, int cy, int r, uint32_t col) {
    gfx_fill_circle(cx, cy, r,       gfx_darken(col, 60));
    gfx_fill_circle(cx, cy, r * 2/3, gfx_darken(col, 25));
    gfx_fill_circle(cx, cy, r / 2,   gfx_lighten(col, 25));
}

// ── HUD chip (rounded pill with text) ──
static inline void gt_chip(int x, int y, int w, int h, uint32_t base,
                           const char* label, uint32_t txt) {
    gfx_fill_gradient_v_rounded(x, y, w, h, h / 2,
                                gfx_lighten(base, 18), gfx_darken(base, 20));
    gfx_draw_rect_rounded_outline(x, y, w, h, h / 2, 1, gfx_lighten(base, 36));
    gfx_draw_string_transparent(x + 8, y + (h - 12) / 2, label, txt);
}

// ── Animated pulse 0..255 (triangle wave, ~1600ms period) ──
static inline int gt_pulse(unsigned long ms) {
    unsigned long p = (ms / 800) % 2;
    unsigned long t = ms % 800;
    int v = p ? (int)(800 - t) : (int)t;
    return v * 255 / 800;
}

#endif // GAME_THEME_H
