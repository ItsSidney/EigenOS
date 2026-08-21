/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef GUI_ANIM_H
#define GUI_ANIM_H

#include <stdint.h>

/* ── Lightweight frame-clock tween used by GUI apps for short
 *    stateful transitions (press scale, panel slide, fade, etc.).
 *    No heap, no central registry: each caller owns one gui_tween_t
 *    and drives it per frame with gui_tween_update(). ────────────── */

#define GUI_ANIM_LINEAR    0   /* constant rate               */
#define GUI_ANIM_EASEOUT   1   /* quadratic ease-out (fast→slow) */
#define GUI_ANIM_EASEINOUT 2   /* quadratic ease-in-out        */

typedef struct {
    uint32_t start_ms;      /* timer_get_ms() snapshot at start        */
    int duration_ms;        /* whole animation length                  */
    float from, to;         /* value domain                            */
    float value;            /* current interpolated value              */
    int   style;            /* one of GUI_ANIM_*                       */
    int   active;           /* currently animating?                    */
} gui_tween_t;

/* Start (or restart) a tween. A duration <= 0 snaps to `to` with no anim. */
void gui_tween_start(gui_tween_t *t, float from, float to, int duration_ms, int style);

/* Advance by real elapsed time; returns the current value. Safe to
 * call on an inactive/never-started tween (returns `to`, or 0.0f). */
float gui_tween_update(gui_tween_t *t);

float gui_tween_value(const gui_tween_t *t);
int   gui_tween_active(const gui_tween_t *t);

/* Finish immediately at `to`. */
void gui_tween_finish(gui_tween_t *t);

/* One-shot normalized progress in [0,1] for a segment started at
 * start_ms over dur ms; handles wraparound and clamping. */
float gui_anim_progress(uint32_t start_ms, int dur);

/* Ease a normalized [0,1] t with the chosen style. */
float gui_anim_ease(float t, int style);

#endif /* GUI_ANIM_H */
