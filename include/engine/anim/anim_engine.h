/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Animation Engine
 *
 * A small, frame-rate-independent 2D animation toolkit. It gives the
 * OS sprite, timing, easing and offscreen-compositing primitives so
 * apps can run smooth 60 fps motion without touching the framebuffer.
 *
 *   - Fixed-step physics substeps (smooth at any redraw rate)
 *   - Sprites with velocity / spin / wall bounce + corner near-miss
 *   - Offscreen canvas with soft-fade trails and rotation blitting
 *   - Color / easing / deterministic RNG helpers
 *********************************************************************/
#ifndef ANIM_ENGINE_H
#define ANIM_ENGINE_H

#include <stdint.h>

/* ===================== Timing ===================== */
/* Call once per visual frame with the monotonic clock. */
void   anim_frame_begin(uint32_t now_ms);
/* Milliseconds since the previous frame (clamped). */
float  anim_frame_dt(void);
/* Smoothed measured frames-per-second. */
float  anim_fps(void);

/* ===================== Color ===================== */
uint32_t anim_lerp_rgb(uint32_t a, uint32_t b, float t);
/* hue in [0,359], sat/val in [0,255]. */
uint32_t anim_hsv(int hue, int sat, int val);
/* Move h by n hues (wrapping), return a hue in [0,359]. */
int      anim_hue_shift(int h, int n);

/* ===================== Math / RNG ===================== */
float    anim_clampf(float v, float lo, float hi);
float    anim_lerpf(float a, float b, float t);
/* Hermite 0..1 -> 0..1. */
float    anim_smoothstep(float t);
uint32_t anim_rng32(uint32_t* state);             /* xorshift32 */
float    anim_randf(uint32_t* state, float lo, float hi);

/* ===================== Sprites ===================== */
typedef struct anim_sprite {
    float x, y;       /* center, in canvas pixels       */
    float vx, vy;     /* velocity, pixels per ms         */
    float w, h;       /* collision half-size *2          */
    float angle;      /* radians, current orientation    */
    float spin;       /* radians per ms                  */
    uint32_t color;   /* fill color of the sprite        */
    uint32_t color_h; /* hue of color above, for cycles  */
} anim_sprite_t;

void anim_sprite_init(anim_sprite_t* s, float x, float y,
                      float vx, float vy, float w, float h);

/* Integrate for dt_ms in fixed 8ms substeps, bouncing off the walls of a
 * [0,0,w,h] field. A positive `guard` makes corner hits impossible: when the
 * sprite drifts within `guard` px of two walls at once, the nearest wall is
 * reflected first so the exact corner is never reached ("scared" bounces).
 *
 * Returns a bitmask of wall hits: bit0 = vertical, bit1 = horizontal.
 * `corners` (optional) is incremented on each corner near-miss. */
int  anim_sprite_bounce(anim_sprite_t* s, float w, float h,
                        float guard, float dt_ms, int* corners);

/* ===================== Canvas ===================== */
/* Offscreen render target — apps draw a scene here, then present it. */
typedef struct anim_canvas {
    uint32_t* px;       /* w*h pixels, own via kmalloc  */
    int w, h;
} anim_canvas_t;

/* Filled triangle (flat- or Gouraud-style per-vertex tint via lerp, but here a
 * solid fill) — span rasterised, clockwise or counter-clockwise winding both
 * accepted. */
void anim_canvas_fill_triangle(anim_canvas_t* c, float x0, float y0,
                               float x1, float y1, float x2, float y2,
                               uint32_t rgb);
/* Thick anti-aliased-free line via fixed-step DDA. */
void anim_canvas_line(anim_canvas_t* c, float x0, float y0, float x1, float y1,
                      int t, uint32_t rgb);
/* Ellipse outline (midpoint algo, thick `t` px). Great for orbital paths. */
void anim_canvas_ellipse_outline(anim_canvas_t* c, float cx, float cy,
                                 float rx, float ry, int t, uint32_t rgb);
/* Soft "glow" disk: a core filled circle feathered to transparent at the
 * edge via concentric rings. Looks like a light bloom — perfect for nuclei /
 * electrons. */
void anim_canvas_glow_disk(anim_canvas_t* c, float cx, float cy, int r,
                           uint32_t core_rgb, uint32_t glow_rgb);

int  anim_canvas_new(anim_canvas_t* c, int w, int h);
void anim_canvas_free(anim_canvas_t* c);
void anim_canvas_clear(anim_canvas_t* c, uint32_t rgb);
/* Multiply every pixel by `keep` (0..1), used for motion trails. */
void anim_canvas_fade(anim_canvas_t* c, float keep);
void anim_canvas_fill_rect(anim_canvas_t* c, int x, int y, int w, int h, uint32_t rgb);
void anim_canvas_rect_outline(anim_canvas_t* c, int x, int y, int w, int h, int t, uint32_t rgb);
void anim_canvas_fill_circle(anim_canvas_t* c, int cx, int cy, int r, uint32_t rgb);
void anim_canvas_circle_outline(anim_canvas_t* c, int cx, int cy, int r, int t, uint32_t rgb);
void anim_canvas_vgrad(anim_canvas_t* c, int x, int y, int w, int h,
                       uint32_t top, uint32_t bottom);
/* Blit a (sw x sh) sprite centered at (cx,cy) in canvas space, rotated by
 * `angle` rad, scaled by `scale`. Pixels with alpha 0 are skipped. */
void anim_canvas_blit(anim_canvas_t* c, const uint32_t* spr, int sw, int sh,
                      float cx, float cy, float angle, float scale);
/* Blit and multiply each opaque sprite pixel by `tint`. */
void anim_canvas_blit_tinted(anim_canvas_t* c, const uint32_t* spr, int sw, int sh,
                             float cx, float cy, float angle, float scale,
                             uint32_t tint);
/* Present the canvas to the framebuffer scaled to the given rect. */
void anim_canvas_present(anim_canvas_t* c, int dx, int dy, int dw, int dh);

#endif /* ANIM_ENGINE_H */