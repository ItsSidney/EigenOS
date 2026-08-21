/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef VECTOR_ICONS_H
#define VECTOR_ICONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Vector Icon Identifiers (Lucide / Feather Icon Suite) ── */
typedef enum {
    ICON_NONE = 0,
    ICON_CALCULATOR,
    ICON_CLOCK,
    ICON_SETTINGS,
    ICON_CALENDAR,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_ROTATE_CCW,    /* Reset / Refresh */
    ICON_FLAG,          /* Lap */
    ICON_BELL,          /* Alarm */
    ICON_CHECK,
    ICON_X,             /* Close / Cancel */
    ICON_PLUS,
    ICON_MINUS,
    ICON_DIVIDE,
    ICON_PERCENT,
    ICON_EQUAL,
    ICON_BACKSPACE,
    ICON_SUN,
    ICON_MOON,
    ICON_SEARCH,
    ICON_PALETTE,       /* Themes */
    ICON_IMAGE,         /* Wallpaper */
    ICON_MONITOR,       /* Display */
    ICON_MOUSE,
    ICON_KEYBOARD,
    ICON_FOLDER,
    ICON_FILE,
    ICON_CHEVRON_LEFT,
    ICON_CHEVRON_RIGHT,
    ICON_CHEVRON_UP,
    ICON_CHEVRON_DOWN,
    ICON_VOLUME,
    ICON_INFO,
    ICON_GRID,          /* Apps / Dock */
    ICON_LAYERS,
    ICON_TERMINAL,
    ICON_GLOBE,
    ICON_POWER,
    ICON_COUNT
} vector_icon_id_t;

/* ── Vector Drawing Primitives on 32-bit Framebuffers ─────── */

/* Draw an icon into a pixel buffer at (x, y) with bounding size `sz` (typically 16, 20, 24, 32, 48) */
void draw_vector_icon(uint32_t* buf, int buf_w, int buf_h,
                      int x, int y, int sz,
                      vector_icon_id_t icon_id, uint32_t color);

/* Draw a line with anti-aliased or multi-pixel thickness */
void draw_vector_line(uint32_t* buf, int buf_w, int buf_h,
                      int x0, int y0, int x1, int y1,
                      int stroke, uint32_t color);

/* Draw a circle outline with stroke width */
void draw_vector_circle(uint32_t* buf, int buf_w, int buf_h,
                        int cx, int cy, int radius,
                        int stroke, uint32_t color);

/* Draw a filled circle */
void draw_vector_disc(uint32_t* buf, int buf_w, int buf_h,
                      int cx, int cy, int radius, uint32_t color);

/* Draw an arc segment (angles in degrees: 0..360) */
void draw_vector_arc(uint32_t* buf, int buf_w, int buf_h,
                     int cx, int cy, int radius,
                     int start_deg, int end_deg,
                     int stroke, uint32_t color);

/* Draw a rounded rectangle outline */
void draw_vector_round_rect(uint32_t* buf, int buf_w, int buf_h,
                            int x, int y, int w, int h, int r,
                            int stroke, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_ICONS_H */
