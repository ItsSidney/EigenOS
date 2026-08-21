/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ────────────────────────────────────────────────────────────────
//  Eigen — Generic vertical scrollbar widget
//  Reusable, frame-driven scroller (track + thumb + click/drag +
//  wheel). See gui_scrollbar.h for usage.
// ────────────────────────────────────────────────────────────────
#include "gui/gui_scrollbar.h"
#include "drivers/video/gfx.h"

int gui_vscrollbar_max(const gui_vscrollbar_t *s) {
    int m = s->content - s->view;
    return m < 0 ? 0 : m;
}

void gui_vscrollbar_init(gui_vscrollbar_t *s, int *value,
        uint32_t bg, uint32_t thumb, uint32_t thumb_hover, uint32_t track) {
    s->x = s->y = s->w = s->h = 0;
    s->content = 0;
    s->view = 1;
    s->value = value;
    s->radius = 3;
    s->dragging = 0;
    s->thumb_hover = 0;
    s->thumb_y = 0;
    s->thumb_h = 0;
    s->color_bg = bg;
    s->color_thumb = thumb;
    s->color_thumb_hover = thumb_hover;
    s->color_track = track;
}

void gui_vscrollbar_clamp(gui_vscrollbar_t *s) {
    if (!s->value) return;
    int m = gui_vscrollbar_max(s);
    if (*s->value < 0) *s->value = 0;
    if (*s->value > m) *s->value = m;
}

void gui_vscrollbar_set(gui_vscrollbar_t *s, int content, int view) {
    if (content < 0) content = 0;
    if (view < 1) view = 1;
    s->content = content;
    s->view = view;
    gui_vscrollbar_clamp(s);
}

void gui_vscrollbar_scroll(gui_vscrollbar_t *s, int delta) {
    if (!s->value) return;
    *s->value += delta;
    gui_vscrollbar_clamp(s);
}

static void gui_vscrollbar_geom(gui_vscrollbar_t *s) {
    int maxv = gui_vscrollbar_max(s);
    if (s->content <= s->view || s->h <= 0) {
        s->thumb_h = s->h;
        s->thumb_y = s->y;
        return;
    }
    int thumb_h = (int)(((long)s->h * s->view) / s->content);
    if (thumb_h < 20) thumb_h = 20;
    if (thumb_h > s->h) thumb_h = s->h;
    int rng = s->h - thumb_h;
    int ty = s->y + (maxv > 0 ? (int)(((long)(*s->value) * rng) / maxv) : 0);
    s->thumb_h = thumb_h;
    s->thumb_y = ty;
}

int gui_vscrollbar_update(gui_vscrollbar_t *s, int mx, int my,
                          int mheld, int mreleased, int wheel_delta) {
    gui_vscrollbar_geom(s);
    int maxv = gui_vscrollbar_max(s);

    if (s->content <= s->view) {
        s->thumb_hover = 0;
        s->dragging = 0;
        return 0;
    }

    int on_thumb = (mx >= s->x && mx < s->x + s->w &&
                    my >= s->thumb_y && my < s->thumb_y + s->thumb_h);
    int on_bar  = (mx >= s->x && mx < s->x + s->w &&
                   my >= s->y && my < s->y + s->h);
    s->thumb_hover = on_thumb;

    if (wheel_delta != 0 && maxv > 0) {
        *s->value -= wheel_delta * 4;       /* wheel up => scroll up */
        gui_vscrollbar_clamp(s);
        gui_vscrollbar_geom(s);
    }

    int span = s->h - 20;
    if (span < 1) span = 1;

    if (s->dragging) {
        if (mheld) {
            int rel = my - s->y - s->h / 2;
            int no = (rel * maxv) / span;
            if (no < 0) no = 0;
            if (no > maxv) no = maxv;
            *s->value = no;
            gui_vscrollbar_geom(s);
        } else {
            s->dragging = 0;
        }
    } else if (on_bar && mheld && maxv > 0) {
        if (on_thumb) {
            s->dragging = 1;
        } else if (s->thumb_h < s->h) {
            int rng = s->h - s->thumb_h;
            int no = ((my - s->y - s->h / 2) * maxv) / rng;
            if (no < 0) no = 0;
            if (no > maxv) no = maxv;
            *s->value = no;
            gui_vscrollbar_geom(s);
        }
    }

    if (mreleased) s->dragging = 0;
    return s->thumb_hover;
}

void gui_vscrollbar_draw(const gui_vscrollbar_t *s) {
    int r = s->radius;
    if (r < 0) r = 0;
    if (s->w > 0 && r > s->w / 2) r = s->w / 2;

    gfx_fill_rect_rounded(s->x, s->y, s->w, s->h, r, s->color_bg);
    gfx_draw_rect_rounded_outline(s->x, s->y, s->w, s->h, r, 1, s->color_track);

    if (s->content <= s->view) return;

    uint32_t tc = s->thumb_hover ? s->color_thumb_hover : s->color_thumb;
    gfx_fill_rect_rounded(s->x, s->thumb_y, s->w, s->thumb_h, r, tc);
}
