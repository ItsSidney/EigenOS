/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef EIGEN_THEME_H
#define EIGEN_THEME_H

#include <stdint.h>

// ============================================================
//  Shared dark-theme tokens for hex viewer + save/load dialogs.
//  One source of truth — swap accent hue here to re-theme all.
// ============================================================

#define FD_BG_CANVAS        0x0d0f12
#define FD_BG_SURFACE       0x14171b
#define FD_BG_SURFACE_ALT   0x191c21
#define FD_BG_SURFACE2      0x121316
#define FD_BORDER_SUBTLE    0x22262c
#define FD_DIVIDER          0x242830

#define FD_TEXT_PRIMARY     0xe6e8eb
#define FD_TEXT_SECONDARY   0x8a8f98
#define FD_TEXT_MUTED       0x7d828b
#define FD_TEXT_TERTIARY    0x5a5f68
#define FD_TEXT_FAINT       0x4e535d

#define FD_ACCENT           0x4da6ff
#define FD_ACCENT_HOVER     0x66b4ff
#define FD_ACCENT_SEL       0x264a6b
#define FD_ACCENT_SEARCH    0xe6b400
#define FD_ACCENT_SEARCH_A  0xffcf40
#define FD_ACCENT_DIFF      0xe05a5a
#define FD_ACCENT_BOOKMARK  0x7c5cff

#define FD_DANGER           0xe05a5a
#define FD_SUCCESS          0x4dc98a

#define FD_DIALOG_BG        0x16181c
#define FD_DIALOG_HEADER    0x1b1e23
#define FD_ROW_HOVER        0x1f232a
#define FD_ROW_SELECTED     0x26374d
#define FD_ROW_ALT          0x13151a
#define FD_INPUT_BG         0x0f1114
#define FD_INPUT_FOCUS      0x4da6ff

#define FD_FOLDER           0xd9a066
#define FD_FOLDER_STRK      0x8a6a40
#define FD_FILE             0xb1b8c4
#define FD_FILE_STRK        0x6c727c

#define FD_SCROLL_BG        0x0e1013
#define FD_SCROLL_THUMB     0x4a505a
#define FD_SCROLL_HOVER     0x6a717c

/* ── Calculator tokens (dark theme) ────────────────────────────
 * Button hierarchy: digits / functions / operators / equals, each
 * with rest + hover states, plus display + state text tokens. The
 * equals key uses the accent hue so it pops from the keypad. ───── */
#define FD_BG_DISPLAY             0x111318
#define FD_BTN_DIGIT              0x1c1f24
#define FD_BTN_DIGIT_HOVER        0x22262c
#define FD_BTN_FUNCTION           0x22262c
#define FD_BTN_FUNCTION_HOVER     0x2b3038
#define FD_BTN_OPERATOR           0x26374d
#define FD_BTN_OPERATOR_ACTIVE    0x2f4a68
#define FD_BTN_EQUALS             0x4da6ff
#define FD_BTN_EQUALS_HOVER       0x65b3ff
#define FD_BTN_EQUALS_PRESS       0x3a8ad9
#define FD_TEXT_DISPLAY_PRIMARY   0xf0f2f4
#define FD_TEXT_DISPLAY_SECONDARY 0x6b7078
#define FD_TEXT_DIGIT             0xe6e8eb
#define FD_TEXT_OPERATOR          0xbcd6f0
#define FD_TEXT_ON_EQUALS         0x0d0f12
#define FD_TEXT_ERROR             0xe05a5a

static inline uint32_t fd_darken(uint32_t rgb, int amt) {
    int r = ((rgb >> 16) & 0xFF) - amt; if (r < 0) r = 0;
    int g = ((rgb >> 8)  & 0xFF) - amt; if (g < 0) g = 0;
    int b = (rgb & 0xFF) - amt;         if (b < 0) b = 0;
    return (r << 16) | (g << 8) | b;
}
static inline uint32_t fd_lighten(uint32_t rgb, int amt) {
    int r = ((rgb >> 16) & 0xFF) + amt; if (r > 255) r = 255;
    int g = ((rgb >> 8)  & 0xFF) + amt; if (g > 255) g = 255;
    int b = (rgb & 0xFF) + amt;         if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

/* ── Graphing Calculator tokens ──────────────────────────────
 * The graph canvas is the one surface in the OS where saturated color
 * is welcome: traces must be individually distinguishable against the
 * near-black bg-graph. All neutral chrome reuses the shared FD_* palette
 * above so the app stays visually unified with the rest of the system.
 * Swap a single accent to re-theme the whole family. ─────────── */
#define GR_BG_GRAPH        0x0a0b0d
#define GR_BG_GRAPH_TOP    0x08080c
#define GR_BG_GRAPH_BOT    0x0b0c10
#define GR_BG_SIDEBAR      0x14171b   /* = FD_BG_SURFACE */
#define GR_BG_KEYPAD       0x191c21   /* = FD_BG_SURFACE_ALT */
#define GR_GRID_MAJOR      0x22262c
#define GR_GRID_MINOR      0x181b1f
#define GR_AXIS_LINE       0x4a4f58
#define GR_AXIS_LABEL      0x8a8f98   /* = FD_TEXT_SECONDARY */
#define GR_BORDER          0x2d2d35
#define GR_CARD            0x141418
#define GR_TOOLTIP_BG      0x1a1a1e
#define GR_CROSSHAIR       0x3a3a44
#define GR_TANGENT         0x888888
#define GR_TRACE_DIM       0xaaaaaa

/* 8-slot trace palette (distinguishable on bg-graph, cycled as added) */
#define GR_TRACE_1  0x4da6ff   /* blue   */
#define GR_TRACE_2  0xff6b6b   /* coral  */
#define GR_TRACE_3  0x4dd88a   /* green  */
#define GR_TRACE_4  0xffb84d   /* amber  */
#define GR_TRACE_5  0xc792ea   /* violet */
#define GR_TRACE_6  0x4dd0e1   /* cyan   */
#define GR_TRACE_7  0xf06292   /* pink   */
#define GR_TRACE_8  0xaed581   /* lime   */

/* ── Scientific Calculator tokens ───────────────────────────
 * The deep calculator: layered (shift/2nd) keypad, mode/status
 * bar, 2D expression display. Keypad DNA from the basic calc
 * (FD_BTN_*) + panel/table patterns from the graphing calc.
 * Modifier keys get their own violet-leaning neutral family. ── */
#define SC_BG_CANVAS            0x0d0f12
#define SC_BG_DISPLAY           0x111318
#define SC_BG_HISTORY           0x14171b   /* = FD_BG_SURFACE */
#define SC_BTN_DIGIT            0x1c1f24
#define SC_BTN_DIGIT_HOVER      0x24282e
#define SC_BTN_FUNCTION         0x22262c
#define SC_BTN_FUNCTION_HOVER   0x2b3038
#define SC_BTN_FUNCTION_SHIFT   0x262a30
#define SC_BTN_OPERATOR         0x26374d
#define SC_BTN_OPERATOR_HOVER   0x2f4a68
#define SC_BTN_EQUALS           0x4da6ff
#define SC_BTN_EQUALS_HOVER     0x65b3ff
#define SC_BTN_MODIFIER         0x2b2530
#define SC_BTN_MODIFIER_HOVER   0x372f3c
#define SC_BTN_MODIFIER_ACTIVE  0x3d3346
#define SC_TEXT_DISPLAY_PRIMARY 0xf0f2f4
#define SC_TEXT_DISPLAY_SEC     0x6b7078
#define SC_TEXT_DIGIT           0xe6e8eb
#define SC_TEXT_FUNCTION_PRIM   0xe6e8eb
#define SC_TEXT_FUNCTION_SHIFT  0x7c8fa8
#define SC_TEXT_ON_EQUALS       0x0d0f12
#define SC_TEXT_ERROR           0xe05a5a
#define SC_ACCENT_PRIMARY       0x4da6ff
#define SC_ACCENT_ANGLE         0x4dd88a

/* ── Calendar tokens (dark theme) ──────────────────────────────
 * Multiple time-scale surfaces (month/week/day/agenda) sharing one
 * token family. The calendar/category slots reuse the graphing
 * calculator's 8-slot trace sequence verbatim (aliased here) so a
 * color means the same thing OS-wide. ───────────────────────── */
#define CAL_BG_CANVAS           0x0d0f12   /* = FD_BG_CANVAS   */
#define CAL_BG_GRID             0x111318   /* month/week/day grid */
#define CAL_BG_SIDEBAR          0x14171b   /* = FD_BG_SURFACE  */
#define CAL_BG_CELL             0x14171b   /* individual day cell */
#define CAL_BG_CELL_TODAY       0x182233
#define CAL_BG_CELL_WEEKEND     0x121417
#define CAL_BG_CELL_OUT         0x0f1114   /* adjacent-month days */
#define CAL_GRID_LINE           0x20242a
#define CAL_TEXT_PRIMARY        0xe6e8eb   /* = FD_TEXT_PRIMARY */
#define CAL_TEXT_MUTED          0x6b7078
#define CAL_TEXT_TODAY          0x4da6ff   /* = FD_ACCENT */
#define CAL_ACCENT              0x4da6ff
#define CAL_NOW_LINE            0xe05a5a   /* = FD_DANGER */
#define CAL_BORDER_SUBTLE       0x22262c   /* = FD_BORDER_SUBTLE */
#define CAL_GRID_MINOR          0x181b1f   /* :30 half-hour tick */

/* Calendar/category slots — same 8 values as GR_TRACE_1..8 */
#define CAL_1  0x4da6ff   /* blue  */
#define CAL_2  0x4dd88a   /* green */
#define CAL_3  0xffb84d   /* amber */
#define CAL_4  0xc792ea   /* violet */
#define CAL_5  0xff6b6b   /* coral */
#define CAL_6  0x4dd0e1   /* cyan  */
#define CAL_7  0xf06292   /* pink  */
#define CAL_8  0xaed581   /* lime  */

/* ── Hex Viewer extended tokens ─────────────────────────── */
#define FD_BYTE_NULL       0x3a3f47   /* null bytes — very dim */
#define FD_BYTE_SPACE      0x6b90a8   /* whitespace — muted blue */
#define FD_BYTE_CTRL       0xc97b4a   /* control chars — muted amber */
#define FD_BYTE_HIGH       0x8a7bc9   /* high-bit — muted violet */
#define FD_BYTE_ASCII_DOT  0x3a3f47   /* non-printable dot */

/* ── Dialog sidebar tokens ───────────────────────────────── */
#define FD_SIDEBAR_BG      0x121316   /* sidebar slightly darker than dialog */
#define FD_SIDEBAR_ACTIVE  0x26374d   /* active sidebar item */
#define FD_SIDEBAR_TEXT    0xc8cdd5   /* sidebar label text */
#define FD_BREADCRUMB_BG   0x1b1e23   /* path bar background */
#define FD_BREADCRUMB_SEP  0x3a3f47   /* breadcrumb > separator */

/* ── Animation / transition helpers ─────────────────────── */
#define FD_ANIM_MS_FAST    100        /* fast hover transitions */
#define FD_ANIM_MS_BASE    150        /* standard transitions */

/* ── Inspector panel tokens ─────────────────────────────── */
#define FD_INSPECTOR_BG    0x111318   /* inspector panel background */
#define FD_INSPECTOR_LABEL 0x5a6170   /* field labels */
#define FD_INSPECTOR_VALUE 0xe6e8eb   /* field values */

/* Blend helper: lerp two colors by t/256 */
static inline uint32_t fd_lerp(uint32_t a, uint32_t b, int t) {
    int ar = (a>>16)&0xFF, ag = (a>>8)&0xFF, ab = a&0xFF;
    int br = (b>>16)&0xFF, bg = (b>>8)&0xFF, bb = b&0xFF;
    int r = ar + ((br - ar) * t >> 8);
    int g = ag + ((bg - ag) * t >> 8);
    int bl2 = ab + ((bb - ab) * t >> 8);
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)bl2;
}

/* ────────────────────────────────────────────────────────────────
 *  PAINT / DRAW EXTENSION PALETTE
 *  A richer, cohesive token system shared by the colour picker,
 *  bitmap maker and the new drawing app. Centralised so every
 *  creative tool stays visually unified with the OS dark theme.
 *  Swap the single ACCENT below to re-theme the whole family.
 * ──────────────────────────────────────────────────────────────── */
#define PT_ACCENT           0x4da6ff   /* primary blue   */
#define PT_ACCENT_2         0x7c5cff   /* violet         */
#define PT_ACCENT_3         0x39d2c0   /* teal           */
#define PT_WARN             0xf0883e   /* amber          */
#define PT_DANGER           0xf85149   /* red            */
#define PT_SUCCESS          0x3fb950   /* green          */

#define PT_CANVAS_BG        0x0a0c10   /* drawing surface backdrop */
#define PT_PAPER            0xffffff   /* white board / paper      */
#define PT_PANEL            0x161a21   /* side / tool panels       */
#define PT_PANEL_ALT        0x1c212b   /* nested panel             */
#define PT_PANEL_HI         0x222a36   /* raised panel             */
#define PT_BORDER           0x30363d   /* hairline borders         */
#define PT_BORDER_STRONG    0x3d4654   /* emphasised borders       */
#define PT_DIVIDER          0x262b33   /* section dividers         */

#define PT_TXT              0xe6edf3   /* primary text   */
#define PT_TXT_2            0xb6c0cc   /* secondary text */
#define PT_TXT_MUTED        0x7d8694   /* muted labels   */
#define PT_TXT_FAINT        0x5a626d   /* faint captions */

#define PT_BTN              0x21262d   /* control rest   */
#define PT_BTN_HOVER        0x2b323c   /* control hover  */
#define PT_BTN_ACTIVE       0x374250   /* control press  */
#define PT_BTN_SEL          0x264a6b   /* selected tool  */

/* Elevation ramp (used for drop shadows / cards) */
#define PT_ELEV_1           0x000000
#define PT_ELEV_2           0x05070a
#define PT_ELEV_3           0x0b0e13

/* Named swatch ramps (for palette strips) */
#define PT_SW_RED           0xf85149
#define PT_SW_ORANGE        0xf0883e
#define PT_SW_AMBER         0xfbbf24
#define PT_SW_YELLOW        0xfde047
#define PT_SW_LIME          0xa3e635
#define PT_SW_GREEN         0x3fb950
#define PT_SW_TEAL          0x2dd4bf
#define PT_SW_CYAN          0x22d3ee
#define PT_SW_SKY           0x38bdf8
#define PT_SW_BLUE          0x4da6ff
#define PT_SW_INDIGO        0x6366f1
#define PT_SW_VIOLET        0x7c5cff
#define PT_SW_PURPLE        0xa855f7
#define PT_SW_PINK          0xf472b6
#define PT_SW_ROSE          0xfb7185
#define PT_SW_BROWN         0x9a6a4a
#define PT_SW_GREY          0x8b949e
#define PT_SW_DARK          0x30363d
#define PT_SW_BLACK         0x0a0c10
#define PT_SW_WHITE         0xffffff

/* Material gray ramp (pencil / sketch tones) */
#define PT_G0 0xffffff
#define PT_G1 0xd9dee6
#define PT_G2 0xb1b8c4
#define PT_G3 0x8b949e
#define PT_G4 0x6d7079
#define PT_G5 0x4a4f58
#define PT_G6 0x30363d
#define PT_G7 0x1b1e23
#define PT_G8 0x0a0c10

/* 16-colour professional painting palette (the default working set) */
static const uint32_t PT_PALETTE[16] = {
    0x0a0c10, 0xffffff, 0xf85149, 0xf0883e, 0xfbbf24, 0x3fb950,
    0x2dd4bf, 0x22d3ee, 0x4da6ff, 0x6366f1, 0x7c5cff, 0xa855f7,
    0xf472b6, 0x9a6a4a, 0x8b949e, 0x4a4f58
};

/* Many-swatch "extended" ramp (used by the picker + draw app) */
static const uint32_t PT_EXTENDED[24] = {
    0xf85149, 0xf0883e, 0xfbbf24, 0xfde047,
    0xa3e635, 0x3fb950, 0x2dd4bf, 0x22d3ee,
    0x38bdf8, 0x4da6ff, 0x6366f1, 0x7c5cff,
    0xa855f7, 0xf472b6, 0xfb7185, 0x9a6a4a,
    0xd9dee6, 0xb1b8c4, 0x8b949e, 0x6d7079,
    0x4a4f58, 0x30363d, 0x1b1e23, 0x0a0c10
};

/* Convenience: pack 0xRRGGBB */
static inline uint32_t pt_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
/* Extract channels */
static inline uint8_t pt_r(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t pt_g(uint32_t c) { return (c >> 8)  & 0xFF; }
static inline uint8_t pt_b(uint32_t c) { return c & 0xFF; }

/* Mix a colour toward white/black by amt (0..255) for shading */
static inline uint32_t pt_shade(uint32_t c, int amt) {
    return fd_lerp(c, amt < 0 ? 0x000000 : 0xffffff, amt < 0 ? -amt : amt);
}

#endif
