/***************************************************************/
/*  EigenUI Theme — implementation                               */
/***************************************************************/
#include "eui_theme.h"
#include <user/eigen.h>   /* EIGEN_THEME_* indices */

static eui_theme g_default;

void eui_theme_preset_light(eui_theme* t) {
    *t = (eui_theme){
        .bg         = EUI_RGB(0xF0,0xF0,0xF0),
        .surface    = EUI_RGB(0xFF,0xFF,0xFF),
        .surface_var= EUI_RGB(0xE4,0xE4,0xE7),
        .text       = EUI_RGB(0x24,0x26,0x2B),
        .on_primary = EUI_RGB(0xFF,0xFF,0xFF),
        .dim        = EUI_RGB(0x6B,0x70,0x78),
        .faint      = EUI_RGB(0x9A,0x9F,0xA6),
        .accent     = EUI_RGB(0x3B,0x82,0xC4),
        .accent2    = EUI_RGB(0x5B,0xA3,0xE0),
        .good       = EUI_RGB(0x4C,0xAF,0x50),
        .bad        = EUI_RGB(0xD9,0x53,0x4F),
        .warning    = EUI_RGB(0xE0,0xA8,0x00),
        .border     = EUI_RGB(0xC8,0xCC,0xD2),
        .shadow     = EUI_RGB(0x00,0x00,0x00),
        .radius     = 6, .radius_small = 4, .padding = 10,
        .border_width = 1, .shadow_blur = 12, .shadow_off = 4,
        .font_size = 14, .font_size_small = 12, .font_size_large = 18,
        .control_h = 26,
    };
}

void eui_theme_preset_dark(eui_theme* t) {
    *t = (eui_theme){
        .bg         = EUI_RGB(0x16,0x18,0x1D),
        .surface    = EUI_RGB(0x24,0x27,0x2E),
        .surface_var= EUI_RGB(0x1B,0x1E,0x24),
        .text       = EUI_RGB(0xE6,0xEA,0xF0),
        .on_primary = EUI_RGB(0xFF,0xFF,0xFF),
        .dim        = EUI_RGB(0x9A,0xA2,0xB0),
        .faint      = EUI_RGB(0x5E,0x66,0x74),
        .accent     = EUI_RGB(0x3B,0x82,0xC4),
        .accent2    = EUI_RGB(0x5B,0xA3,0xE0),
        .good       = EUI_RGB(0x4C,0xAF,0x50),
        .bad        = EUI_RGB(0xE5,0x5B,0x57),
        .warning    = EUI_RGB(0xE0,0xA8,0x00),
        .border     = EUI_RGB(0x3A,0x3F,0x48),
        .shadow     = EUI_RGB(0x00,0x00,0x00),
        .radius     = 6, .radius_small = 4, .padding = 10,
        .border_width = 1, .shadow_blur = 14, .shadow_off = 4,
        .font_size = 14, .font_size_small = 12, .font_size_large = 18,
        .control_h = 26,
    };
}

eui_theme* eui_theme_default(void) {
    static int inited = 0;
    if (!inited) { eui_theme_preset_light(&g_default); inited = 1; }
    return &g_default;
}
void eui_theme_set_default(const eui_theme* t) { if (t) g_default = *t; }

void eui_theme_from_os(eui_theme* t, const uint32_t* os, int n) {
    if (!t || !os) return;
    /* start from a sensible base so missing slots still look fine */
    eui_theme_preset_light(t);
    #define G(i) ((i) < n ? os[(i)] : EUI_NONE)
    if (G(EIGEN_THEME_BG)        != EUI_NONE) t->bg     = G(EIGEN_THEME_BG);
    if (G(EIGEN_THEME_SURFACE)   != EUI_NONE) t->surface= G(EIGEN_THEME_SURFACE);
    if (G(EIGEN_THEME_SURFACE_VAR)!= EUI_NONE) t->surface_var = G(EIGEN_THEME_SURFACE_VAR);
    if (G(EIGEN_THEME_PRIMARY)   != EUI_NONE) t->text   = G(EIGEN_THEME_PRIMARY);
    if (G(EIGEN_THEME_SECONDARY) != EUI_NONE) t->dim    = G(EIGEN_THEME_SECONDARY);
    if (G(EIGEN_THEME_TERTIARY)  != EUI_NONE) t->faint  = G(EIGEN_THEME_TERTIARY);
    if (G(EIGEN_THEME_ERROR)     != EUI_NONE) t->bad    = G(EIGEN_THEME_ERROR);
    if (G(EIGEN_THEME_OUTLINE)   != EUI_NONE) t->border = G(EIGEN_THEME_OUTLINE);
    if (G(EIGEN_THEME_ACCENT)    != EUI_NONE) t->accent = G(EIGEN_THEME_ACCENT);
    if (G(EIGEN_THEME_WINTITLE)  != EUI_NONE) t->text   = G(EIGEN_THEME_WINTITLE);
    #undef G
}
