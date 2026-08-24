/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef GFX_H
#define GFX_H

#include <stdint.h>

// ============================================================
//  Eigen — Premium Graphics Engine
//  Full 32-bit RGB drawing primitives for modern UI rendering
// ============================================================
#define CHAR_W 8
#define CHAR_H 16
extern int gfx_scale;

// ── Dynamic Theme System ──────────────────────────────────
typedef enum {
    CLR_ID_BG_DARK,
    CLR_ID_BG_SURFACE,
    CLR_ID_BG_CARD,
    CLR_ID_BG_HOVER,
    CLR_ID_BORDER,
    CLR_ID_BORDER_LIGHT,
    CLR_ID_TEXT_PRIMARY,
    CLR_ID_TEXT_SECONDARY,
    CLR_ID_TEXT_TERTIARY,
    CLR_ID_TEXT_DIM,
    CLR_ID_TASKBAR,
    CLR_ID_TASKBAR_GLOW,
    CLR_ID_WINDOW_TITLE,
    CLR_ID_SHADOW
} theme_color_id_t;

// To avoid circular dependency, get_theme_color is declared in gui.h
// and we assume it's available where gfx.h is used.
extern uint32_t get_theme_color(int color_id);

#define CLR_BG_DARK        get_theme_color(CLR_ID_BG_DARK)
#define CLR_BG_SURFACE     get_theme_color(CLR_ID_BG_SURFACE)
#define CLR_BG_CARD        get_theme_color(CLR_ID_BG_CARD)
#define CLR_BG_HOVER       get_theme_color(CLR_ID_BG_HOVER)
#define CLR_BORDER         get_theme_color(CLR_ID_BORDER)
#define CLR_BORDER_LIGHT   get_theme_color(CLR_ID_BORDER_LIGHT)

#define CLR_TEXT_PRIMARY    get_theme_color(CLR_ID_TEXT_PRIMARY)
#define CLR_TEXT_SECONDARY  get_theme_color(CLR_ID_TEXT_SECONDARY)
#define CLR_TEXT_TERTIARY   get_theme_color(CLR_ID_TEXT_TERTIARY)
#define CLR_TEXT_DIM        get_theme_color(CLR_ID_TEXT_DIM)

#define CLR_ACCENT_BLUE    0x58A6FF   // Primary accent
#define CLR_ACCENT_GREEN   0x3FB950   // Success green
#define CLR_ACCENT_PURPLE  0xBC8CFF   // Purple accent
#define CLR_ACCENT_ORANGE  0xF0883E   // Warning orange
#define CLR_ACCENT_RED     0xF85149   // Error red
#define CLR_ACCENT_CYAN    0x39D2C0   // Teal accent
#define CLR_ACCENT_YELLOW  0xE3B341   // Gold/yellow accent
#define CLR_ACCENT_PINK    0xF778BA   // Pink accent

#define CLR_TASKBAR        get_theme_color(CLR_ID_TASKBAR)
#define CLR_TASKBAR_GLOW   get_theme_color(CLR_ID_TASKBAR_GLOW)
#define CLR_HOVER          0x1F6FEB   // Hover state blue
#define CLR_WINDOW_TITLE   get_theme_color(CLR_ID_WINDOW_TITLE)
#define CLR_SHADOW         get_theme_color(CLR_ID_SHADOW)

#define CLR_WIN_CLOSE      0xF85149   // Window close dot
#define CLR_WIN_MIN        0xE3B341   // Window minimize dot
#define CLR_WIN_MAX        0x3FB950   // Window maximize dot

// Wallpaper gradient stops
#define CLR_WALL_TOP       0x0A0E1A   // Deep navy
#define CLR_WALL_MID       0x141B3D   // Indigo
#define CLR_WALL_BOT       0x1A1040   // Dark purple

// ── Utility Macros ──────────────────────────────────────────
#define RGB_R(c) (((c) >> 16) & 0xFF)
#define RGB_G(c) (((c) >> 8) & 0xFF)
#define RGB_B(c) ((c) & 0xFF)
#define RGB(r,g,b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

// ── Drawing Primitives ──────────────────────────────────────

// Clipping
void gfx_set_clip(int x, int y, int w, int h);
void gfx_push_clip(int x, int y, int w, int h);
void gfx_pop_clip(void);
void gfx_reset_clip(void);
void gfx_get_clip(int* x0, int* y0, int* x1, int* y1);

// Basic shapes
void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void gfx_fill_rect_rounded(int x, int y, int w, int h, int radius, uint32_t rgb);
void gfx_draw_rect_outline(int x, int y, int w, int h, int thickness, uint32_t rgb);
void gfx_draw_rect_rounded_outline(int x, int y, int w, int h, int radius, int thickness, uint32_t rgb);
void gfx_draw_bevel_rect(int x, int y, int w, int h, uint32_t bg, int sunken);
void gfx_draw_switch(int x, int y, int w, int h, int state);

// Gradients
void gfx_fill_gradient_v(int x, int y, int w, int h, uint32_t rgb_top, uint32_t rgb_bottom);
void gfx_fill_gradient_h(int x, int y, int w, int h, uint32_t rgb_left, uint32_t rgb_right);
void gfx_fill_gradient_v_rounded(int x, int y, int w, int h, int radius, uint32_t rgb_top, uint32_t rgb_bottom);

// Shadows & blending
void gfx_draw_shadow(int x, int y, int w, int h, int shadow_size);
void gfx_blend_pixel(int x, int y, uint32_t rgb, uint8_t alpha);

// Lines & circles
void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb);
void gfx_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t rgb);
void gfx_draw_hline(int x, int y, int w, uint32_t rgb);
void gfx_draw_vline(int x, int y, int h, uint32_t rgb);
void gfx_draw_circle(int cx, int cy, int r, uint32_t rgb);
void gfx_fill_circle(int cx, int cy, int r, uint32_t rgb);
void gfx_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb);
void gfx_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb);

// ── Text Rendering (pixel-positioned) ───────────────────────
void gfx_draw_char(int px, int py, char c, uint32_t fg_rgb, uint32_t bg_rgb);
void gfx_draw_char_transparent(int px, int py, char c, uint32_t fg_rgb);
void gfx_draw_string(int px, int py, const char* str, uint32_t fg_rgb, uint32_t bg_rgb);
void gfx_draw_string_transparent(int px, int py, const char* str, uint32_t fg_rgb);
/* Anti-aliased scaled text (crisp large numbers; blends fg over bg). */
void gfx_draw_string_scaled_aa(int x, int y, const char* str, uint32_t fg, uint32_t bg, int scale);

// ── High-Level UI Components ────────────────────────────────
// Window chrome: draws title bar with dots, shadow, rounded top
void gfx_draw_window(int x, int y, int w, int h, const char* title, uint32_t accent_color);
void gfx_draw_window_custom(int x, int y, int w, int h, const char* title, uint32_t accent_color, int radius);
// Rounded button
void gfx_draw_button(int x, int y, int w, int h, const char* label, uint32_t bg_rgb, uint32_t fg_rgb, int selected);
// Status pill badge (small rounded label)
void gfx_draw_pill(int x, int y, const char* label, uint32_t bg_rgb, uint32_t fg_rgb);
// Progress bar
void gfx_draw_progress_bar(int x, int y, int w, int h, int percent, uint32_t fill_rgb, uint32_t bg_rgb);
// Separator line
void gfx_draw_separator(int x, int y, int w, uint32_t rgb);

// ── Utility ─────────────────────────────────────────────────
uint32_t gfx_lerp_color(uint32_t c1, uint32_t c2, int t, int max);
uint32_t gfx_darken(uint32_t rgb, int amount);
uint32_t gfx_lighten(uint32_t rgb, int amount);
int gfx_strlen(const char* s);

// ── Image / Sprite Rendering ────────────────────────────────
#define GFX_SPRITE_KEY 0xFF00FF  // transparent pixel marker

void gfx_draw_rgb_bitmap(int x, int y, int w, int h, const uint8_t* rgb_data);
void gfx_draw_rgb_bitmap_scaled(int x, int y, int dw, int dh, const uint8_t* rgb_data, int sw, int sh);
void gfx_draw_rgb_bitmap_keyed(int x, int y, int dw, int dh, const uint8_t* rgb_data, int sw, int sh);
void gfx_draw_sprite(int x, int y, const uint32_t* src, int sw, int sh);
void gfx_draw_sprite_scaled(int x, int y, int dw, int dh, const uint32_t* src, int sw, int sh);
void gfx_draw_image(int x, int y, int w, int h, const uint8_t* data);

// ── Adwaita RGBA Icon Rendering ─────────────────────────────
void gfx_draw_icon_rgba(int x, int y, int size, const uint32_t* rgba_data);
void gfx_draw_icon_1bit(int x, int y, const uint8_t* data, int w, int h, uint32_t color);

#endif
