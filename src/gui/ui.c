/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — Sound Settings UI
// ============================================================
#include "gui/ui.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard.h"
#include "gui/wm.h"
#include "drivers/audio/audio.h"
#include "gui/gui.h"
#include <stddef.h>
#include <stdint.h>

static int g_sound_win = -1;

static uint32_t blend_colors(uint32_t a, uint32_t b, int t) {
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + ((br - ar) * t) / 255;
    int g = ag + ((bg - ag) * t) / 255;
    int bl = ab + ((bb - ab) * t) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void sound_on_key(int id, char key) {
    if (key == 27) {
        g_sound_win = -1;
        wm_close_window(id);
    }
}

static void sound_on_mute(int win_id, int btn_id) {
    (void)win_id; (void)btn_id;
    audio_set_mute(!audio_is_muted());
}

static void sound_vol_down(int win_id, int btn_id) {
    (void)win_id; (void)btn_id;
    audio_set_master_volume(audio_get_master_volume() - 5);
}
static void sound_vol_up(int win_id, int btn_id) {
    (void)win_id; (void)btn_id;
    audio_set_master_volume(audio_get_master_volume() + 5);
}

/* Click / drag on the gradient bar sets the volume. */
static void sound_bar_mouse(int id, int mx, int my, int mb) {
    (void)mb;
    wm_window_t* win = wm_get_window(id);
    if (!win) return;
    int bar_x = win->x + 20, bar_y = win->y + 110, bar_w = win->w - 40, bar_h = 16;
    if (my >= bar_y - 6 && my <= bar_y + bar_h + 6) {
        int rel = mx - bar_x;
        if (rel < 0) rel = 0;
        if (rel > bar_w) rel = bar_w;
        audio_set_master_volume((rel * 100) / bar_w);
        if (audio_is_muted()) audio_set_mute(0);
    }
}

static void sound_render(int id, int x, int y, int w, int h, int vx, int vy) {
    (void)id; (void)h; (void)vx;
    uint32_t bg       = theme_get_color(THEME_ROLE_WINDOW_BG);
    uint32_t text_clr = theme_get_color(THEME_ROLE_PRIMARY);
    uint32_t muted    = theme_get_color(THEME_ROLE_SECONDARY);
    int vol = audio_get_master_volume();

    /* Mouse delivery (replaces legacy on_mouse): drag on the gradient bar
       adjusts volume while the pointer is over this window. */
    if (wm_window_at_point(wm_mouse_x(), wm_mouse_y()) == g_sound_win &&
        (wm_mouse_left_clicked() || wm_mouse_left_held())) {
        sound_bar_mouse(id, wm_mouse_x(), wm_mouse_y(),
                        wm_mouse_left_held() ? 1 : 0);
    }

    gfx_fill_rect(x, y, w, h, bg);
    gfx_draw_string_transparent(x + 20, y + 20 - vy, "SOUND SETTINGS", text_clr);

    char buf[16];
    int n = 0;
    if (vol >= 100) buf[n++] = '0' + (vol / 100); vol %= 100;
    if (n || vol >= 10) buf[n++] = '0' + (vol / 10); vol %= 10;
    buf[n++] = '0' + vol;
    buf[n++] = '%';
    buf[n++] = 0;
    gfx_draw_string_transparent(x + (w - n * 8) / 2, y + 48 - vy, buf, text_clr);
    gfx_draw_string_transparent(x + 20, y + 66 - vy, audio_is_muted() ? "MUTED" : "ACTIVE", muted);

    int bar_x = x + 20, bar_y = y + 110 - vy, bar_w = w - 40, bar_h = 16;
    gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x333333);
    int fill_w = audio_is_muted() ? 0 : (audio_get_master_volume() * bar_w) / 100;
    gfx_fill_rect(bar_x, bar_y, fill_w, bar_h, text_clr);
    gfx_draw_rect_outline(bar_x, bar_y, bar_w, bar_h, 1, muted);

    int btn_y = bar_y + bar_h + 24;
    gfx_fill_rect(x + 20, btn_y, 80, 28, bg);
    gfx_draw_string_transparent(x + 32, btn_y + 8, "VOL -", text_clr);
    gfx_fill_rect(x + 120, btn_y, 80, 28, bg);
    gfx_draw_string_transparent(x + 132, btn_y + 8, "VOL +", text_clr);
    gfx_fill_rect(x + 220, btn_y, 100, 28, text_clr);
    gfx_draw_string_transparent(x + 232, btn_y + 8, audio_is_muted() ? "UNMUTE" : "MUTE", bg);
}

void show_sound_settings(void) {
    if (g_sound_win >= 0) {
        wm_close_window(g_sound_win);
        g_sound_win = -1;
        return;
    }

    uint32_t fw = get_fb_width(), fh = get_fb_height();
    uint32_t text_clr = theme_get_color(THEME_ROLE_PRIMARY);
    int w = 340, h = 200;
    int x = (fw - w) / 2;
    int y = (fh - h) / 2;

    g_sound_win = wm_open_window(x, y, w, h, "Sound Settings", 0x6E7681,
                                 sound_render, sound_on_key, NULL);

    wm_add_button(g_sound_win, 1, 20, 150, 80, 28, "VOL -", theme_get_color(THEME_ROLE_BUTTON_BG), theme_get_color(THEME_ROLE_BUTTON_TEXT), sound_vol_down);
    wm_add_button(g_sound_win, 2, 120, 150, 80, 28, "VOL +", theme_get_color(THEME_ROLE_BUTTON_BG), theme_get_color(THEME_ROLE_BUTTON_TEXT), sound_vol_up);
    wm_add_button(g_sound_win, 3, 220, 150, 100, 28, "MUTE", 0x333333, text_clr, sound_on_mute);
}
