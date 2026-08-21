/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ────────────────────────────────────────────────────────────────
//  Eigen — Tiny frame-clock tween for GUI transitions.
//  See gui_anim.h. Timer-backed; one gui_tween_t per property.
// ────────────────────────────────────────────────────────────────
#include "gui/gui_anim.h"
#include "kernel/time/timer.h"

float gui_anim_ease(float t, int style) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    switch (style) {
        case GUI_ANIM_EASEINOUT:
            return (t < 0.5f)
                ? (2.0f * t * t)
                : (1.0f - 2.0f * (1.0f - t) * (1.0f - t));
        case GUI_ANIM_EASEOUT:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case GUI_ANIM_LINEAR:
        default:
            return t;
    }
}

float gui_anim_progress(uint32_t start_ms, int dur) {
    if (dur <= 0) return 1.0f;
    uint32_t now = timer_get_ms();
    uint32_t el = now - start_ms;
    float t = (float)el / (float)dur;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

void gui_tween_start(gui_tween_t *t, float from, float to, int duration_ms, int style) {
    t->from = from;
    t->to = to;
    t->duration_ms = duration_ms;
    t->style = style;
    t->start_ms = timer_get_ms();
    if (duration_ms <= 0) {
        t->value = to;
        t->active = 0;
    } else {
        t->value = from;
        t->active = 1;
    }
}

float gui_tween_update(gui_tween_t *t) {
    if (!t->active) {
        return (t->value != 0.0f || t->to != 0.0f) ? t->to : 0.0f;
    }
    uint32_t now = timer_get_ms();
    uint32_t el = now - t->start_ms;
    float tt;
    if (t->duration_ms <= 0) {
        tt = 1.0f;
    } else {
        tt = (float)el / (float)t->duration_ms;
        if (tt < 0.0f) tt = 0.0f;
        if (tt > 1.0f) tt = 1.0f;
    }
    t->value = t->from + (t->to - t->from) * gui_anim_ease(tt, t->style);
    if (tt >= 1.0f) {
        t->value = t->to;
        t->active = 0;
    }
    return t->value;
}

void gui_tween_finish(gui_tween_t *t) {
    t->value = t->to;
    t->active = 0;
}

float gui_tween_value(const gui_tween_t *t) { return t->value; }
int   gui_tween_active(const gui_tween_t *t) { return t->active; }
