/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "gui/gui.h"
#include "filesystem/filesystem.h"
#include <string.h>
#include <stdio.h>

static personalization_t prefs;
personalization_t* get_personalization(void) { return &prefs; }

static uint32_t g_custom_colors[THEME_ROLE_COUNT];

/* ══════════════════════════════════════════════════════════════════════
 * Theme 0 — VOID
 * The purest dark. No warmth, no hue. Cold deep black surfaces, platinum
 * chrome, cool mid-grey text. Silent and clinical.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_VOID_DEF = {
    "Void",
    {
        [THEME_ROLE_BACKGROUND]        = 0x050507,
        [THEME_ROLE_SURFACE]           = 0x0D0D10,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x18181C,
        [THEME_ROLE_PRIMARY]           = 0xE8E8EE,
        [THEME_ROLE_ON_PRIMARY]        = 0x050507,
        [THEME_ROLE_SECONDARY]         = 0x888898,
        [THEME_ROLE_ON_SECONDARY]      = 0xE8E8EE,
        [THEME_ROLE_TERTIARY]          = 0x505060,
        [THEME_ROLE_ERROR]             = 0xE05555,
        [THEME_ROLE_OUTLINE]           = 0x252528,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x202024,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xE8E8EE,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x050507,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x383840,
        [THEME_ROLE_DISABLED]          = 0x404048,
        [THEME_ROLE_BUTTON_BG]         = 0x18181C,
        [THEME_ROLE_BUTTON_TEXT]       = 0xE8E8EE,
        [THEME_ROLE_BUTTON_HOVER]      = 0x252528,
        [THEME_ROLE_MENU_BG]           = 0x0D0D10,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x18181C,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x252528,
        [THEME_ROLE_WINDOW_BG]         = 0x0D0D10,
        [THEME_ROLE_WINDOW_TITLE]      = 0x18181C,
        [THEME_ROLE_WINDOW_BORDER]     = 0x252528,
        [THEME_ROLE_TASKBAR_BG]        = 0x050507,
        [THEME_ROLE_TASKBAR_TEXT]      = 0xC0C0CC,
        [THEME_ROLE_ACCENT]            = 0xE8E8EE,
    },
    { .titlebar_h = 30, .corner_radius = 0, .btn_style = 0,
      .title_grad_a = 0, .title_grad_b = 0, .winbtn_layout = 0,
      .close_hover = 0xE05555, .close_only = 0 },
    0
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 1 — EMBER
 * Warm charcoal browns, amber glow. Like a workspace lit by candlelight.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_EMBER_DEF = {
    "Ember",
    {
        [THEME_ROLE_BACKGROUND]        = 0x0E0B08,
        [THEME_ROLE_SURFACE]           = 0x161210,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x201A16,
        [THEME_ROLE_PRIMARY]           = 0xEEDFCC,
        [THEME_ROLE_ON_PRIMARY]        = 0x0E0B08,
        [THEME_ROLE_SECONDARY]         = 0xA0876A,
        [THEME_ROLE_ON_SECONDARY]      = 0xEEDFCC,
        [THEME_ROLE_TERTIARY]          = 0x6A5040,
        [THEME_ROLE_ERROR]             = 0xD44030,
        [THEME_ROLE_OUTLINE]           = 0x342820,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x2A2018,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xEEDFCC,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x0E0B08,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x5A4030,
        [THEME_ROLE_DISABLED]          = 0x504038,
        [THEME_ROLE_BUTTON_BG]         = 0x201A16,
        [THEME_ROLE_BUTTON_TEXT]       = 0xEEDFCC,
        [THEME_ROLE_BUTTON_HOVER]      = 0x2E2418,
        [THEME_ROLE_MENU_BG]           = 0x161210,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x2A2018,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0xD4780A,
        [THEME_ROLE_WINDOW_BG]         = 0x161210,
        [THEME_ROLE_WINDOW_TITLE]      = 0x201A16,
        [THEME_ROLE_WINDOW_BORDER]     = 0x403020,
        [THEME_ROLE_TASKBAR_BG]        = 0x0E0B08,
        [THEME_ROLE_TASKBAR_TEXT]      = 0xC8A880,
        [THEME_ROLE_ACCENT]            = 0xE88A20,  /* amber */
    },
    { .titlebar_h = 30, .corner_radius = 6, .btn_style = 0,
      .title_grad_a = 0x2A2018, .title_grad_b = 0x161210, .winbtn_layout = 0,
      .close_hover = 0xD44030, .close_only = 0 },
    11  /* warm amber accent */
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 2 — SLATE
 * Cold steel-blue dark. Precision and focus. Ice-highlight chrome.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_SLATE_DEF = {
    "Slate",
    {
        [THEME_ROLE_BACKGROUND]        = 0x090C12,
        [THEME_ROLE_SURFACE]           = 0x10141C,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x181E28,
        [THEME_ROLE_PRIMARY]           = 0xD4E4F8,
        [THEME_ROLE_ON_PRIMARY]        = 0x090C12,
        [THEME_ROLE_SECONDARY]         = 0x6888B0,
        [THEME_ROLE_ON_SECONDARY]      = 0xD4E4F8,
        [THEME_ROLE_TERTIARY]          = 0x3A5070,
        [THEME_ROLE_ERROR]             = 0xE05060,
        [THEME_ROLE_OUTLINE]           = 0x202A3A,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x1C2838,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xD4E4F8,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x090C12,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x3A5070,
        [THEME_ROLE_DISABLED]          = 0x304050,
        [THEME_ROLE_BUTTON_BG]         = 0x181E28,
        [THEME_ROLE_BUTTON_TEXT]       = 0xD4E4F8,
        [THEME_ROLE_BUTTON_HOVER]      = 0x222E42,
        [THEME_ROLE_MENU_BG]           = 0x10141C,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x1C2838,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x284E88,
        [THEME_ROLE_WINDOW_BG]         = 0x10141C,
        [THEME_ROLE_WINDOW_TITLE]      = 0x181E28,
        [THEME_ROLE_WINDOW_BORDER]     = 0x2A3A54,
        [THEME_ROLE_TASKBAR_BG]        = 0x090C12,
        [THEME_ROLE_TASKBAR_TEXT]      = 0x9AB8D8,
        [THEME_ROLE_ACCENT]            = 0x78C8F8,  /* ice blue */
    },
    { .titlebar_h = 30, .corner_radius = 4, .btn_style = 0,
      .title_grad_a = 0x1C2838, .title_grad_b = 0x10141C, .winbtn_layout = 0,
      .close_hover = 0xE05060, .close_only = 0 },
    6  /* light blue accent */
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 3 — CARBON
 * Industrial graphite. Precise white chrome on near-black panels.
 * The developer's theme — sharp, no-nonsense, dense.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_CARBON_DEF = {
    "Carbon",
    {
        [THEME_ROLE_BACKGROUND]        = 0x0A0A0A,
        [THEME_ROLE_SURFACE]           = 0x141414,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x1E1E1E,
        [THEME_ROLE_PRIMARY]           = 0xF0F0F0,
        [THEME_ROLE_ON_PRIMARY]        = 0x0A0A0A,
        [THEME_ROLE_SECONDARY]         = 0x909090,
        [THEME_ROLE_ON_SECONDARY]      = 0xF0F0F0,
        [THEME_ROLE_TERTIARY]          = 0x606060,
        [THEME_ROLE_ERROR]             = 0xFF4444,
        [THEME_ROLE_OUTLINE]           = 0x2E2E2E,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x242424,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xF0F0F0,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x0A0A0A,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x484848,
        [THEME_ROLE_DISABLED]          = 0x484848,
        [THEME_ROLE_BUTTON_BG]         = 0x1E1E1E,
        [THEME_ROLE_BUTTON_TEXT]       = 0xF0F0F0,
        [THEME_ROLE_BUTTON_HOVER]      = 0x2E2E2E,
        [THEME_ROLE_MENU_BG]           = 0x141414,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x202020,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x303030,
        [THEME_ROLE_WINDOW_BG]         = 0x141414,
        [THEME_ROLE_WINDOW_TITLE]      = 0x1E1E1E,
        [THEME_ROLE_WINDOW_BORDER]     = 0x383838,
        [THEME_ROLE_TASKBAR_BG]        = 0x080808,
        [THEME_ROLE_TASKBAR_TEXT]      = 0xC8C8C8,
        [THEME_ROLE_ACCENT]            = 0xF0F0F0,  /* pure white chrome */
    },
    { .titlebar_h = 28, .corner_radius = 0, .btn_style = 0,
      .title_grad_a = 0, .title_grad_b = 0, .winbtn_layout = 0,
      .close_hover = 0xFF4444, .close_only = 0 },
    1  /* white accent */
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 4 — AURORA
 * Deep midnight navy, teal-green glow. The feeling of cold northern
 * skies — organic and alive.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_AURORA_DEF = {
    "Aurora",
    {
        [THEME_ROLE_BACKGROUND]        = 0x060C10,
        [THEME_ROLE_SURFACE]           = 0x0C1418,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x121E22,
        [THEME_ROLE_PRIMARY]           = 0xC8EEE8,
        [THEME_ROLE_ON_PRIMARY]        = 0x060C10,
        [THEME_ROLE_SECONDARY]         = 0x5A9090,
        [THEME_ROLE_ON_SECONDARY]      = 0xC8EEE8,
        [THEME_ROLE_TERTIARY]          = 0x2A6060,
        [THEME_ROLE_ERROR]             = 0xE06060,
        [THEME_ROLE_OUTLINE]           = 0x183030,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x102828,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xC8EEE8,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x060C10,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x2A6060,
        [THEME_ROLE_DISABLED]          = 0x284848,
        [THEME_ROLE_BUTTON_BG]         = 0x121E22,
        [THEME_ROLE_BUTTON_TEXT]       = 0xC8EEE8,
        [THEME_ROLE_BUTTON_HOVER]      = 0x1C2E34,
        [THEME_ROLE_MENU_BG]           = 0x0C1418,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x102828,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x1A5858,
        [THEME_ROLE_WINDOW_BG]         = 0x0C1418,
        [THEME_ROLE_WINDOW_TITLE]      = 0x102020,
        [THEME_ROLE_WINDOW_BORDER]     = 0x1E4040,
        [THEME_ROLE_TASKBAR_BG]        = 0x060C10,
        [THEME_ROLE_TASKBAR_TEXT]      = 0x80C8B8,
        [THEME_ROLE_ACCENT]            = 0x3CE8C8,  /* teal aurora */
    },
    { .titlebar_h = 30, .corner_radius = 8, .btn_style = 0,
      .title_grad_a = 0x102020, .title_grad_b = 0x0C1418, .winbtn_layout = 0,
      .close_hover = 0xE06060, .close_only = 0 },
    9  /* teal accent */
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 5 — CRIMSON
 * Near-pure black with deep crimson-red highlights. Intense and focused.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_CRIMSON_DEF = {
    "Crimson",
    {
        [THEME_ROLE_BACKGROUND]        = 0x090608,
        [THEME_ROLE_SURFACE]           = 0x120D0F,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x1C1316,
        [THEME_ROLE_PRIMARY]           = 0xF0D8D8,
        [THEME_ROLE_ON_PRIMARY]        = 0x090608,
        [THEME_ROLE_SECONDARY]         = 0xA05858,
        [THEME_ROLE_ON_SECONDARY]      = 0xF0D8D8,
        [THEME_ROLE_TERTIARY]          = 0x6A2A30,
        [THEME_ROLE_ERROR]             = 0xFF5555,
        [THEME_ROLE_OUTLINE]           = 0x2C1C1E,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x241418,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xF0D8D8,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x090608,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x6A2A30,
        [THEME_ROLE_DISABLED]          = 0x483038,
        [THEME_ROLE_BUTTON_BG]         = 0x1C1316,
        [THEME_ROLE_BUTTON_TEXT]       = 0xF0D8D8,
        [THEME_ROLE_BUTTON_HOVER]      = 0x2C1820,
        [THEME_ROLE_MENU_BG]           = 0x120D0F,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x241418,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x8A1828,
        [THEME_ROLE_WINDOW_BG]         = 0x120D0F,
        [THEME_ROLE_WINDOW_TITLE]      = 0x1C1316,
        [THEME_ROLE_WINDOW_BORDER]     = 0x3A1A20,
        [THEME_ROLE_TASKBAR_BG]        = 0x090608,
        [THEME_ROLE_TASKBAR_TEXT]      = 0xC88080,
        [THEME_ROLE_ACCENT]            = 0xE03050,  /* deep red */
    },
    { .titlebar_h = 30, .corner_radius = 4, .btn_style = 0,
      .title_grad_a = 0x1C1316, .title_grad_b = 0x120D0F, .winbtn_layout = 0,
      .close_hover = 0xFF5555, .close_only = 0 },
    23  /* deep red accent */
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 6 — OBSIDIAN
 * Near-black with subtle violet undertones. Surfaces feel like polished
 * volcanic glass. Silver chrome, muted mauve accents.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_OBSIDIAN_DEF = {
    "Obsidian",
    {
        [THEME_ROLE_BACKGROUND]        = 0x080810,
        [THEME_ROLE_SURFACE]           = 0x0F0F18,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x181824,
        [THEME_ROLE_PRIMARY]           = 0xDCDCF0,
        [THEME_ROLE_ON_PRIMARY]        = 0x080810,
        [THEME_ROLE_SECONDARY]         = 0x7878A8,
        [THEME_ROLE_ON_SECONDARY]      = 0xDCDCF0,
        [THEME_ROLE_TERTIARY]          = 0x484868,
        [THEME_ROLE_ERROR]             = 0xE06060,
        [THEME_ROLE_OUTLINE]           = 0x222232,
        [THEME_ROLE_OVERLAY]           = 0x000000,
        [THEME_ROLE_SURFACE_TINT]      = 0x1E1E2E,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xDCDCF0,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x080810,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x484868,
        [THEME_ROLE_DISABLED]          = 0x404058,
        [THEME_ROLE_BUTTON_BG]         = 0x181824,
        [THEME_ROLE_BUTTON_TEXT]       = 0xDCDCF0,
        [THEME_ROLE_BUTTON_HOVER]      = 0x222232,
        [THEME_ROLE_MENU_BG]           = 0x0F0F18,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x1E1E2E,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x383858,
        [THEME_ROLE_WINDOW_BG]         = 0x0F0F18,
        [THEME_ROLE_WINDOW_TITLE]      = 0x181824,
        [THEME_ROLE_WINDOW_BORDER]     = 0x2E2E48,
        [THEME_ROLE_TASKBAR_BG]        = 0x080810,
        [THEME_ROLE_TASKBAR_TEXT]      = 0xAAAAC8,
        [THEME_ROLE_ACCENT]            = 0xC0B0F0,  /* soft mauve-silver */
    },
    { .titlebar_h = 30, .corner_radius = 6, .btn_style = 0,
      .title_grad_a = 0x1E1E2E, .title_grad_b = 0x0F0F18, .winbtn_layout = 0,
      .close_hover = 0xE06060, .close_only = 0 },
    8  /* violet-silver accent */
};

/* ══════════════════════════════════════════════════════════════════════
 * Theme 7 — NORD  (factory default)
 * Polar-night blues matched to the Nordzy icon theme. Square, compact,
 * standard chrome: zero corner radius, minimal buttons, frost accent.
 * ════════════════════════════════════════════════════════════════════ */
static const theme_def_t THEME_MIST_DEF = {
    "Nord",
    {
        [THEME_ROLE_BACKGROUND]        = 0x2E3440,
        [THEME_ROLE_SURFACE]           = 0x3B4252,
        [THEME_ROLE_SURFACE_VARIANT]   = 0x434C5E,
        [THEME_ROLE_PRIMARY]           = 0xECEFF4,
        [THEME_ROLE_ON_PRIMARY]        = 0x2E3440,
        [THEME_ROLE_SECONDARY]         = 0x9CA6B8,
        [THEME_ROLE_ON_SECONDARY]      = 0xECEFF4,
        [THEME_ROLE_TERTIARY]          = 0x616E81,
        [THEME_ROLE_ERROR]             = 0xBF616A,
        [THEME_ROLE_OUTLINE]           = 0x4C566A,
        [THEME_ROLE_OVERLAY]           = 0x242933,
        [THEME_ROLE_SURFACE_TINT]      = 0x3F495C,
        [THEME_ROLE_INVERSE_SURFACE]   = 0xECEFF4,
        [THEME_ROLE_INVERSE_ON_SURFACE]= 0x2E3440,
        [THEME_ROLE_SHADOW]            = 0x000000,
        [THEME_ROLE_SCROLLBAR]         = 0x4C566A,
        [THEME_ROLE_DISABLED]          = 0x5B667A,
        [THEME_ROLE_BUTTON_BG]         = 0x3B4252,
        [THEME_ROLE_BUTTON_TEXT]       = 0xECEFF4,
        [THEME_ROLE_BUTTON_HOVER]      = 0x434C5E,
        [THEME_ROLE_MENU_BG]           = 0x333B49,
        [THEME_ROLE_MENU_ITEM_HOVER]   = 0x3F4859,
        [THEME_ROLE_MENU_ITEM_SELECTED]= 0x465064,
        [THEME_ROLE_WINDOW_BG]         = 0x3B4252,
        [THEME_ROLE_WINDOW_TITLE]      = 0x333B49,
        [THEME_ROLE_WINDOW_BORDER]     = 0x434C5E,
        [THEME_ROLE_TASKBAR_BG]        = 0x272C36,
        [THEME_ROLE_TASKBAR_TEXT]      = 0xD8DEE9,
        [THEME_ROLE_ACCENT]            = 0xE5E9F0,
    },
    { .titlebar_h = 28, .corner_radius = 0, .btn_style = 0,
      .title_grad_a = 0, .title_grad_b = 0, .winbtn_layout = 0,
      .close_hover = 0xBF616A, .close_only = 0 },
    1   /* platinum snow */
};

/* ── Theme table ─────────────────────────────────────────────────────── */
static const theme_def_t* g_themes[THEME_COUNT] = {
    &THEME_VOID_DEF,
    &THEME_EMBER_DEF,
    &THEME_SLATE_DEF,
    &THEME_CARBON_DEF,
    &THEME_AURORA_DEF,
    &THEME_CRIMSON_DEF,
    &THEME_OBSIDIAN_DEF,
    &THEME_MIST_DEF,
};

static theme_id_t g_active = THEME_VOID;

/* ── User-defined themes (Settings → Themes) ───────────────────────────
 * Created at runtime from a colour-wheel pick, stored in cfg/themes.cfg
 * so they survive reboots. Slots are appended after the built-ins, so a
 * user theme's id = THEME_COUNT + slot. Names live in writable buffers
 * (theme_def_t.name is `const char*`). */
static theme_def_t g_user[USER_THEME_MAX];
static char       g_user_names[USER_THEME_MAX][24];
static int        g_user_n = 0;

int theme_user_count(void) { return g_user_n; }
int theme_total(void)      { return THEME_COUNT + g_user_n; }
int theme_is_user(theme_id_t id) {
    return id >= THEME_COUNT && id < theme_total();
}

theme_def_t* theme_def(theme_id_t id) {
    if (id < 0) return (theme_def_t*)&THEME_VOID_DEF;
    if (id < THEME_COUNT) return (theme_def_t*)g_themes[id];
    int slot = id - THEME_COUNT;
    if (slot >= 0 && slot < g_user_n) return &g_user[slot];
    return (theme_def_t*)&THEME_VOID_DEF;
}

theme_id_t  theme_current(void) { return g_active; }
const char* theme_name(theme_id_t id) { return theme_def(id)->name; }

/* ── User theme persistence (cfg/themes.cfg) ──────────────────────────
 * Format (one theme per line, all values decimal for easy round-trip):
 *   name|role0,role1,...role35|th,cr,btn,font,ga,gb,layout,ch,co|accent
 */
static void write_user_themes_cfg(void) {
    fs_mkdir("cfg");
    fs_delete("cfg/themes.cfg");
    if (g_user_n == 0) return;
    int fd = fs_create("cfg/themes.cfg");
    if (fd < 0) return;
    char b[2304];
    for (int i = 0; i < g_user_n; i++) {
        theme_def_t* t = &g_user[i];
        int n = snprintf(b, sizeof b, "%s|", t->name);
        for (int r2 = 0; r2 < THEME_ROLE_COUNT && n < (int)sizeof b - 24; r2++)
            n += snprintf(b + n, sizeof b - (size_t)n, "%s%u",
                          r2 ? "," : "", t->palette[r2] & 0xFFFFFF);
        wm_theme_params_t* w = &t->wm;
        n += snprintf(b + n, sizeof b - (size_t)n,
                      "|%d,%d,%d,%d,%u,%u,%d,%u,%d|%d\n",
                      w->titlebar_h, w->corner_radius, w->btn_style, w->font_scale,
                      w->title_grad_a & 0xFFFFFF, w->title_grad_b & 0xFFFFFF,
                      w->winbtn_layout, w->close_hover & 0xFFFFFF, w->close_only,
                      t->accent_idx);
        fs_write(fd, b, (uint32_t)n);
    }
    fs_close(fd);
}

static int dec_at(const char* s, int* out) {
    int v = 0, got = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; got = 1; }
    if (!got) return -1;
    *out = v;
    return 0;
}

static void load_user_themes_cfg(void) {
    g_user_n = 0;
    int fd = fs_open("cfg/themes.cfg", 0);
    if (fd < 0) return;
    char buf[8192];
    int r = fs_read(fd, buf, (int)sizeof(buf) - 1);
    fs_close(fd);
    if (r <= 0) return;
    buf[r] = 0;

    int p = 0;
    while (p < r && g_user_n < USER_THEME_MAX) {
        int e = p; while (e < r && buf[e] != '\n') e++;
        char* line = &buf[p];
        int ln = e - p;
        int nl = 0;
        while (nl < ln && line[nl] != '|') nl++;
        if (nl <= 0 || nl >= (int)sizeof(g_user_names[0])) goto next;
        memcpy(g_user_names[g_user_n], line, (size_t)nl);
        g_user_names[g_user_n][nl] = 0;
        int q = nl + 1;
        int bad = 0;
        for (int r2 = 0; r2 < THEME_ROLE_COUNT && !bad; r2++) {
            if (r2 > 0) { if (q >= ln || line[q] != ',') { bad = 1; break; } q++; }
            int v = 0;
            if (dec_at(line + q, &v) < 0 || v < 0 || v > 0xFFFFFF) { bad = 1; break; }
            g_user[g_user_n].palette[r2] = (uint32_t)v;
            while (q < ln && line[q] >= '0' && line[q] <= '9') q++;
        }
        if (bad) goto next;
        if (q >= ln || line[q] != '|') goto next;
        q++;
        int wm_vals[9];
        for (int w2 = 0; w2 < 9 && !bad; w2++) {
            if (w2 > 0) { if (q >= ln || line[q] != ',') { bad = 1; break; } q++; }
            int v = 0;
            if (dec_at(line + q, &v) < 0) { bad = 1; break; }
            wm_vals[w2] = v;
            while (q < ln && line[q] >= '0' && line[q] <= '9') q++;
        }
        if (bad) goto next;
        if (q >= ln || line[q] != '|') goto next;
        q++;
        int ac = 0;
        if (dec_at(line + q, &ac) < 0) goto next;
        wm_theme_params_t* w = &g_user[g_user_n].wm;
        w->titlebar_h     = wm_vals[0];
        w->corner_radius  = wm_vals[1];
        w->btn_style      = wm_vals[2];
        w->font_scale     = wm_vals[3];
        w->title_grad_a   = (uint32_t)wm_vals[4];
        w->title_grad_b   = (uint32_t)wm_vals[5];
        w->winbtn_layout  = wm_vals[6];
        w->close_hover    = (uint32_t)wm_vals[7];
        w->close_only     = wm_vals[8];
        g_user[g_user_n].accent_idx = ac;
        g_user[g_user_n].name = g_user_names[g_user_n];
        g_user_n++;
    next:
        p = (e < r) ? e + 1 : r + 1;
    }
}

theme_id_t theme_user_add(const char* name, const uint32_t* palette,
                          const wm_theme_params_t* wm, int accent_idx) {
    if (g_user_n >= USER_THEME_MAX) return -1;
    if (!name || !name[0] || !palette) return -1;
    int nn = 0; while (name[nn] && nn < (int)sizeof(g_user_names[0]) - 1) nn++;
    memcpy(g_user_names[g_user_n], name, (size_t)nn);
    g_user_names[g_user_n][nn] = 0;
    memcpy(g_user[g_user_n].palette, palette, THEME_ROLE_COUNT * sizeof(uint32_t));
    if (wm) g_user[g_user_n].wm = *wm;
    else {
        memset(&g_user[g_user_n].wm, 0, sizeof(wm_theme_params_t));
        g_user[g_user_n].wm.titlebar_h = 30;
        g_user[g_user_n].wm.corner_radius = 6;
        g_user[g_user_n].wm.close_hover = 0xE05555;
    }
    g_user[g_user_n].accent_idx = accent_idx;
    g_user[g_user_n].name = g_user_names[g_user_n];
    theme_id_t id = (theme_id_t)(THEME_COUNT + g_user_n);
    g_user_n++;
    write_user_themes_cfg();
    return id;
}

void theme_user_del(theme_id_t id) {
    if (!theme_is_user(id)) return;
    int slot = id - THEME_COUNT;
    for (int i = slot; i < g_user_n - 1; i++) {
        g_user[i] = g_user[i + 1];
        memcpy(g_user_names[i], g_user_names[i + 1],
               sizeof(g_user_names[i]));
        g_user[i].name = g_user_names[i];
    }
    g_user_n--;
    if (g_active >= THEME_COUNT && g_active >= theme_total()) {
        g_active = THEME_VOID;
        prefs.accent_color_idx = theme_def(g_active)->accent_idx;
    }
    write_user_themes_cfg();
}

static void write_theme_cfg(theme_id_t id) {
    fs_mkdir("cfg");
    fs_delete("cfg/theme.cfg");
    int fd = fs_create("cfg/theme.cfg");
    if (fd < 0) return;
    char b[8]; int n = snprintf(b, sizeof b, "%d\n", (int)id);
    fs_write(fd, b, n);
    fs_close(fd);
}

void theme_apply(theme_id_t id) {
    if (id < 0 || id >= theme_total()) return;
    g_active = id;
    prefs.accent_color_idx = theme_def(id)->accent_idx;
    theme_reset_custom();
    write_theme_cfg(id);
}

void theme_load_cfg(void) {
    load_user_themes_cfg();   /* user themes first: ids extend the built-ins */
    int fd = fs_open("cfg/theme.cfg", 0);
    if (fd < 0) { g_active = THEME_MIST; return; }   /* factory default */
    char b[16]; int n = fs_read(fd, b, 15); b[n < 15 ? n : 15] = 0;
    fs_close(fd);
    int v = 0;
    for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + (b[i] - '0');
    if (v >= 0 && v < theme_total()) {
        g_active = (theme_id_t)v;
        prefs.accent_color_idx = theme_def(g_active)->accent_idx;
    } else {
        g_active = THEME_MIST;
    }
    theme_reset_custom();
}

uint32_t theme_get_color(theme_role_t role) {
    if (role < 0 || role >= THEME_ROLE_COUNT) return 0xFFFFFF;
    if (g_custom_colors[role] != 0) return g_custom_colors[role];
    return theme_def(g_active)->palette[role];
}

void theme_set_custom_color(theme_role_t role, uint32_t color) {
    if (role < 0 || role >= THEME_ROLE_COUNT) return;
    g_custom_colors[role] = color;
}

void theme_reset_custom(void) {
    memset(g_custom_colors, 0, sizeof(g_custom_colors));
}

/* Live, system-wide accent override. Writes cfg/accent.cfg so the choice
 * survives reboots. Used by Personalization → Accent. It does NOT touch the
 * active theme's palette spelling, just redirects the shared accent table. */
static void write_accent_cfg(int idx) {
    fs_mkdir("cfg");
    fs_delete("cfg/accent.cfg");
    int fd = fs_create("cfg/accent.cfg");
    if (fd < 0) return;
    char b[12]; int n = snprintf(b, sizeof b, "%d\n", idx);
    fs_write(fd, b, n);
    fs_close(fd);
}

void theme_set_accent(int idx) {
    /* The accent table lives inside get_accent_color(); mirror the index into
     * prefs.accent_color_idx (which that function reads) and persist. */
    prefs.accent_color_idx = idx;
    theme_reset_custom();   /* drop any one-off custom role overrides */
    write_accent_cfg(idx);
}

void theme_load_accent_cfg(void) {
    int fd = fs_open("cfg/accent.cfg", 0);
    if (fd < 0) return;
    char b[16]; int n = fs_read(fd, b, 15); b[n < 15 ? n : 15] = 0;
    fs_close(fd);
    int v = 0;
    for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + (b[i] - '0');
    if (v >= 0) prefs.accent_color_idx = v;
}

void theme_get_wm_params(wm_theme_params_t* out) {
    if (!out) return;
    *out = theme_def(g_active)->wm;
}

uint32_t get_accent_color(void) {
    static const uint32_t accent_colors[] = {
        0xE8E8EE, 0xF0F0F0, 0xCCCCCC, 0xAAAAAA, 0x888888, 0x666666,
        0x8AB4F8, 0x81C995, 0xC58AF9, 0x3CE8C8, 0xF28B82, 0xE88A20,
        0x669DF6, 0x5BB974, 0xAF5CF7, 0x4ECDC4, 0xE8EAED, 0x9AA0A6,
        0xFF6B6B, 0x4ECDC4, 0xFFE66D, 0xFF9F1C, 0x2EC4B6, 0xE03050,
        0x8B5CF6, 0x3A86FF, 0xFB5607, 0xFFBE0B, 0xC0B0F0, 0xFF006E
    };
    if ((size_t)prefs.accent_color_idx < sizeof(accent_colors)/sizeof(accent_colors[0]))
        return accent_colors[prefs.accent_color_idx];
    return accent_colors[0];
}

uint32_t accent_color_at(int idx) {
    static const uint32_t accent_colors[] = {
        0xE8E8EE, 0xF0F0F0, 0xCCCCCC, 0xAAAAAA, 0x888888, 0x666666,
        0x8AB4F8, 0x81C995, 0xC58AF9, 0x3CE8C8, 0xF28B82, 0xE88A20,
        0x669DF6, 0x5BB974, 0xAF5CF7, 0x4ECDC4, 0xE8EAED, 0x9AA0A6,
        0xFF6B6B, 0x4ECDC4, 0xFFE66D, 0xFF9F1C, 0x2EC4B6, 0xE03050,
        0x8B5CF6, 0x3A86FF, 0xFB5607, 0xFFBE0B, 0xC0B0F0, 0xFF006E
    };
    if (idx < 0 || (size_t)idx >= sizeof(accent_colors)/sizeof(accent_colors[0]))
        return 0;
    return accent_colors[idx];
}

void theme_init(void) {
    memset(&prefs, 0, sizeof(prefs));
    prefs.accent_color_idx = 0;
    prefs.clock_24h = 1;
    prefs.mouse_sensitivity = 2;
    prefs.theme = 1;
    prefs.bg_idx = 0;
    prefs.bg_pattern = 0;
    prefs.bg_pattern_size = 1;
    prefs.accent_idx = 0;
    prefs.font_idx = 0;
    prefs.btn_idx = 0;
    prefs.corner_radius = 0;
    prefs.font_size = 1;
    prefs.contrast = 0;
    prefs.saturation = 0;
    prefs.transparency = 0;
    prefs.wallpaper_id = 0;
    prefs.wallpaper_mode = 0;
    prefs.wallpaper_file[0] = 0;
    /* restore the last-applied wallpaper (or default to the first shipped
     * package, wp5) so the OS never boots into the plain procedural gradient */
    extern void wallpaper_mgr_apply_default(void);
    wallpaper_mgr_apply_default();
    prefs.taskbar_pinned_mask = 0;
    prefs.desktop_icons_mask = 0;
    memset(g_custom_colors, 0, sizeof(g_custom_colors));
    theme_load_accent_cfg();   /* restore persistent accent choice (cfg/accent.cfg) */
    g_active = THEME_MIST;     /* factory default: modern light grey */
}
