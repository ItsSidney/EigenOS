/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/
/*
 * Mac-style minimal top taskbar.
 *   • Top-only, 26px tall, always visible.
 *   • Thin bar: "EigenOS" label + "Apps" dropdown on the left.
 *   • Dropdown: categories → apps (uses menu_app_entries from gui.h).
 *   • NO start menu, NO clock, NO graphs, NO notification bell,
 *     NO desktop switcher, NO pinned app icons — just the Apps dropdown.
 */

#include "gui/gui.h"
#include "gui/wm.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/mouse.h"
#include <stdint.h>

#define MAC_TASKBAR_H   26
#define MAC_DD_W        180
#define MAC_ROW_H       26

/* Dropdown state */
#define MAC_DROPDOWN_CLOSED  0
#define MAC_DROPDOWN_CATS    1
#define MAC_DROPDOWN_APPS    2
static int g_mac_dropdown_state = MAC_DROPDOWN_CLOSED;
static int g_mac_selected_cat = 0;

static const char* g_mac_cat_names[8] = {
    "All Apps", "Productivity", "System", "Games",
    "Graphics", "Debug", "Accessibility", "Networking"
};

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

static int mac_collect_apps(int cat, int out_idx[64]) {
    int n = 0;
    for (int i = 0; menu_app_entries[i].name != 0 && n < 64; i++) {
        if (cat == 0 || menu_app_entries[i].category == cat)
            out_idx[n++] = i;
    }
    return n;
}

void draw_taskbar_mac(void) {
    uint32_t fw = get_fb_width();
    if (fw == 0) return;

    uint32_t tb_bg  = theme_get_color(THEME_ROLE_TASKBAR_BG);
    uint32_t tb_txt = theme_get_color(THEME_ROLE_TASKBAR_TEXT);
    uint32_t tb_sel = theme_get_color(THEME_ROLE_MENU_ITEM_SELECTED);
    uint32_t tb_hov = theme_get_color(THEME_ROLE_MENU_ITEM_HOVER);
    uint32_t tb_mut = theme_get_color(THEME_ROLE_SECONDARY);
    uint32_t border = theme_get_color(THEME_ROLE_OUTLINE);
    uint32_t acc    = get_accent_color();

    int mx = mouse_get_x(), my = mouse_get_y();
    int by = 0;

    /* Thin top bar, full width */
    gfx_fill_rect(0, by, (int)fw, MAC_TASKBAR_H, tb_bg);
    gfx_draw_hline(0, by + MAC_TASKBAR_H - 1, (int)fw, border);

    /* "EigenOS" label */
    gfx_draw_string_transparent(12, by + 8, "EigenOS", tb_txt);

    /* "Apps" dropdown trigger */
    int apps_x = 90, apps_w = 48;
    int apps_hover = point_in_rect(mx, my, apps_x, by + 4, apps_w, MAC_TASKBAR_H - 6);
    if (apps_hover || g_mac_dropdown_state != MAC_DROPDOWN_CLOSED)
        gfx_fill_rect_rounded(apps_x - 2, by + 4, apps_w + 4, MAC_TASKBAR_H - 7, 4,
                              apps_hover ? tb_hov : tb_sel);
    gfx_draw_string_transparent(apps_x + 2, by + 8, "Apps",
                                g_mac_dropdown_state != MAC_DROPDOWN_CLOSED
                                ? 0xFFFFFF : (apps_hover ? tb_txt : tb_mut));

    /* ── Dropdown menu ── */
    int dd_x = 4, dd_y = by + MAC_TASKBAR_H + 4;

    if (g_mac_dropdown_state == MAC_DROPDOWN_CATS) {
        int dd_h = 8 * MAC_ROW_H;
        gfx_fill_rect_rounded(dd_x, dd_y, MAC_DD_W, dd_h, 8, 0x1A1A1D);
        gfx_draw_rect_rounded_outline(dd_x, dd_y, MAC_DD_W, dd_h, 8, 1, border);
        for (int c = 0; c < 8; c++) {
            int cy = dd_y + c * MAC_ROW_H;
            int hov = point_in_rect(mx, my, dd_x, cy, MAC_DD_W, MAC_ROW_H);
            if (hov) gfx_fill_rect_alpha(dd_x, cy, MAC_DD_W, MAC_ROW_H, tb_hov, 200);
            gfx_draw_string_transparent(dd_x + 10, cy + 8, g_mac_cat_names[c],
                                        hov ? 0xFFFFFF : tb_mut);
        }
    } else if (g_mac_dropdown_state == MAC_DROPDOWN_APPS) {
        int apps_idx[64];
        int ac = mac_collect_apps(g_mac_selected_cat, apps_idx);
        int dd_h = (ac > 0 ? ac * MAC_ROW_H : MAC_ROW_H) + MAC_ROW_H;
        gfx_fill_rect_rounded(dd_x, dd_y, MAC_DD_W, dd_h, 8, 0x1A1A1D);
        gfx_draw_rect_rounded_outline(dd_x, dd_y, MAC_DD_W, dd_h, 8, 1, border);
        gfx_fill_rect_alpha(dd_x, dd_y, MAC_DD_W, MAC_ROW_H, acc, 30);
        gfx_draw_hline(dd_x, dd_y + MAC_ROW_H, MAC_DD_W, border);
        gfx_draw_string_transparent(dd_x + 10, dd_y + 8,
                                    g_mac_cat_names[g_mac_selected_cat], 0xFFFFFF);
        for (int i = 0; i < ac; i++) {
            int ay = dd_y + MAC_ROW_H + i * MAC_ROW_H;
            int hov = point_in_rect(mx, my, dd_x, ay, MAC_DD_W, MAC_ROW_H);
            if (hov) gfx_fill_rect_alpha(dd_x + 1, ay, MAC_DD_W - 2, MAC_ROW_H, tb_hov, 220);
            gfx_draw_string_transparent(dd_x + 10, ay + 8,
                                        menu_app_entries[apps_idx[i]].name,
                                        hov ? 0xFFFFFF : tb_txt);
        }
    }

    /* ── Interaction ── */
    int clicked = wm_mouse_left_clicked();

    if (clicked && apps_hover) {
        g_mac_dropdown_state = MAC_DROPDOWN_CATS;
        g_mac_selected_cat = 0;
    }

    if (g_mac_dropdown_state == MAC_DROPDOWN_CATS) {
        int dd_h = 8 * MAC_ROW_H;
        for (int c = 0; c < 8; c++) {
            int cy = dd_y + c * MAC_ROW_H;
            if (clicked && point_in_rect(mx, my, dd_x, cy, MAC_DD_W, MAC_ROW_H)) {
                g_mac_selected_cat = c;
                g_mac_dropdown_state = MAC_DROPDOWN_APPS;
            }
        }
        if (clicked && !point_in_rect(mx, my, dd_x, dd_y, MAC_DD_W, dd_h))
            g_mac_dropdown_state = MAC_DROPDOWN_CLOSED;
    }

    if (g_mac_dropdown_state == MAC_DROPDOWN_APPS) {
        int apps_idx[64];
        int ac = mac_collect_apps(g_mac_selected_cat, apps_idx);
        int dd_h = (ac > 0 ? ac * MAC_ROW_H : MAC_ROW_H) + MAC_ROW_H;
        if (clicked && point_in_rect(mx, my, dd_x, dd_y, MAC_DD_W, MAC_ROW_H))
            g_mac_dropdown_state = MAC_DROPDOWN_CATS;
        for (int i = 0; i < ac; i++) {
            int ay = dd_y + MAC_ROW_H + i * MAC_ROW_H;
            if (clicked && point_in_rect(mx, my, dd_x, ay, MAC_DD_W, MAC_ROW_H)) {
                app_item_t* app = &menu_app_entries[apps_idx[i]];
                if (app->launch_func)
                    app->launch_func();
                g_mac_dropdown_state = MAC_DROPDOWN_CLOSED;
            }
        }
        if (clicked && !point_in_rect(mx, my, dd_x, dd_y, MAC_DD_W, dd_h))
            g_mac_dropdown_state = MAC_DROPDOWN_CLOSED;
    }
}
