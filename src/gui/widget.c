/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — Desktop Widget
//  Floating system info card for the desktop
// ============================================================
#include "gui/widget.h"
#include "drivers/video/gfx.h"

void widget_draw_sysinfo(int screen_w, int screen_h, int hour, int minute, int second) {
    int w = 200;
    int h = 80;
    int x = screen_w - w - 20;
    int y = 20;

    // Shadow
    gfx_draw_shadow(x, y, w, h, 4);

    // Card background (semi-transparent dark)
    gfx_fill_rect_rounded(x, y, w, h, 6, CLR_BG_CARD);
    gfx_draw_rect_rounded_outline(x, y, w, h, 6, 1, CLR_BORDER);

    // Time display — large centered
    char time_str[9];
    time_str[0] = (hour / 10) + '0';
    time_str[1] = (hour % 10) + '0';
    time_str[2] = ':';
    time_str[3] = (minute / 10) + '0';
    time_str[4] = (minute % 10) + '0';
    time_str[5] = ':';
    time_str[6] = (second / 10) + '0';
    time_str[7] = (second % 10) + '0';
    time_str[8] = 0;

    // Time text centered
    int time_w = 8 * 8; // 8 chars * 8px
    gfx_draw_string_transparent(x + (w - time_w) / 2, y + 12, time_str, CLR_ACCENT_BLUE);

    // System name
    gfx_draw_string_transparent(x + 16, y + 36, "Eigen v5.2.0", CLR_TEXT_SECONDARY);

    // Architecture
    gfx_draw_string_transparent(x + 16, y + 54, "x86_64 UEFI", CLR_TEXT_TERTIARY);
}
