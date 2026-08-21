/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef GUI_SCROLLBAR_H
#define GUI_SCROLLBAR_H

#include <stdint.h>

/* ── Generic vertical scrollbar widget ──────────────────────────
 * A self-contained, frame-driven vertical scroller. The caller owns
 * the backing scroll value (e.g. a row offset) and feeds the widget
 * its geometry + the current mouse state each frame.
 *
 * usage:
 *   gui_vscrollbar_init(&vsb, &my_offset, colors...);
 *   gui_vscrollbar_set(&vsb, content_rows, visible_rows);
 *   vsb.x = ...; vsb.y = ...; vsb.w = ...; vsb.h = ...;
 *   gui_vscrollbar_update(&vsb, mx, my, mheld, mreleased, wheel);
 *   gui_vscrollbar_draw(&vsb);
 * ──────────────────────────────────────────────────────────────── */
typedef struct {
    int x, y, w, h;          /* bar rect: top-left corner, thickness w, length h */
    int content;             /* total content units (e.g. rows) */
    int view;                /* visible units */
    int *value;              /* external scroll offset — owned by caller */
    int radius;              /* corner radius of track + thumb */
    int dragging;            /* thumb is being dragged */
    int thumb_hover;         /* mouse is over the thumb (cached by update) */
    int thumb_y;             /* cached thumb y (set by update) */
    int thumb_h;             /* cached thumb height (set by update) */
    uint32_t color_bg;       /* track background */
    uint32_t color_thumb;
    uint32_t color_thumb_hover;
    uint32_t color_track;    /* outer outline */
} gui_vscrollbar_t;

void gui_vscrollbar_init(gui_vscrollbar_t *s, int *value,
        uint32_t bg, uint32_t thumb, uint32_t thumb_hover, uint32_t track);

int  gui_vscrollbar_max(const gui_vscrollbar_t *s);
void gui_vscrollbar_set(gui_vscrollbar_t *s, int content, int view);
void gui_vscrollbar_clamp(gui_vscrollbar_t *s);
void gui_vscrollbar_scroll(gui_vscrollbar_t *s, int delta);

/* Feed input: returns 1 if the thumb is currently hovered. */
int  gui_vscrollbar_update(gui_vscrollbar_t *s, int mx, int my,
                            int mheld, int mreleased, int wheel_delta);

void gui_vscrollbar_draw(const gui_vscrollbar_t *s);

#endif /* GUI_SCROLLBAR_H */
