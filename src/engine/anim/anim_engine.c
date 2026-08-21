/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Animation Engine (implementation)
 *********************************************************************/
#include "engine/anim/anim_engine.h"
#include "kernel/mem/kheap.h"
#include "kernel/time/timer.h"
#include "drivers/video/gfx.h"
#include <string.h>
#include <math.h>

/* ===================== Timing ===================== */

static uint32_t s_last_ms = 0;
static float    s_avg_fps = 60.0f;
static float    s_frame_dt = 16.0f;

void anim_frame_begin(uint32_t now_ms) {
    if (s_last_ms == 0) { s_last_ms = now_ms; return; }
    uint32_t d = now_ms - s_last_ms;
    if (d > 50) d = 50;
    s_last_ms = now_ms;
    if (d == 0) d = 1;
    s_frame_dt = (float)d;
    float fps = 1000.0f / (float)d;
    s_avg_fps = s_avg_fps * 0.9f + fps * 0.1f;
}

float anim_frame_dt(void) { return s_frame_dt; }

float anim_fps(void) { return s_avg_fps; }

/* ===================== Color ===================== */

uint32_t anim_lerp_rgb(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int r = (int)((a >> 16 & 0xFF) + (((b >> 16 & 0xFF) - (a >> 16 & 0xFF)) * t));
    int g = (int)((a >> 8  & 0xFF) + (((b >> 8  & 0xFF) - (a >> 8  & 0xFF)) * t));
    int bb = (int)((a       & 0xFF) + (((b       & 0xFF) - (a       & 0xFF)) * t));
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (bb < 0) bb = 0; if (bb > 255) bb = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bb;
}

uint32_t anim_hsv(int hue, int sat, int val) {
    hue = hue % 360; if (hue < 0) hue += 360;
    int hi = hue / 60;
    int f  = hue % 60;
    int p  = val * (255 - sat) / 255;
    int q  = val * (255 - f * sat / 60) / 255;
    int t  = val * (255 - (60 - f) * sat / 60) / 255;
    int v  = val;
    int r, g, b;
    switch (hi) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default: r=v; g=p; b=q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int anim_hue_shift(int h, int n) {
    h += n; h %= 360; if (h < 0) h += 360;
    return h;
}

/* ===================== Math / RNG ===================== */

float anim_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float anim_lerpf(float a, float b, float t) { return a + (b - a) * t; }

float anim_smoothstep(float t) {
    t = anim_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

uint32_t anim_rng32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

float anim_randf(uint32_t* state, float lo, float hi) {
    return lo + (float)(anim_rng32(state) % 10000) / 9999.0f * (hi - lo);
}

/* ===================== Sprites ===================== */

void anim_sprite_init(anim_sprite_t* s, float x, float y,
                      float vx, float vy, float w, float h) {
    s->x = x; s->y = y;
    s->vx = vx; s->vy = vy;
    s->w = w; s->h = h;
    s->angle = 0.0f; s->spin = 0.0f;
    s->color = 0xFFFFFF; s->color_h = 0;
}

/* Reflect one wall bounce. If `guard > 0` and two walls are about to be hit,
 * the nearer one is reflected first — a few pixels from the corner — so the
 * exact corner point is never reached. Returns hit bits. */
static int reflect_walls(anim_sprite_t* s, float fw, float fh, float guard, int* corners) {
    /* Rotated AABB extents: keep the whole rotated badge inside the field so
       it never gets clipped at the walls (constant apparent size). */
    float ca = (float)cos((double)s->angle), sa = (float)sin((double)s->angle);
    float hw = (s->w * (float)fabs(ca) + s->h * (float)fabs(sa)) * 0.5f;
    float hh = (s->w * (float)fabs(sa) + s->h * (float)fabs(ca)) * 0.5f;
    float l = hw, r = fw - hw, t = hh, b = fh - hh;
    if (r < l) { l = fw * 0.25f; r = fw * 0.75f; }
    if (b < t) { t = fh * 0.25f; b = fh * 0.75f; }

    int bits = 0;

    if (guard > 0 && corners) {
        float dl = s->x - l, dr = r - s->x, dt = s->y - t, db = b - s->y;
        int near_left  = s->vx < 0 && dl >= 0 && dl <= guard;
        int near_right = s->vx > 0 && dr >= 0 && dr <= guard;
        int near_top   = s->vy < 0 && dt >= 0 && dt <= guard;
        int near_bot   = s->vy > 0 && db >= 0 && db <= guard;

        /* Corner near-miss: heading into a pair of walls at once. Reflect the
           axis with the shortest run left — it never quite reaches the joint. */
        if ((near_left && (near_top || near_bot)) || (near_right && (near_top || near_bot))) {
            float d_h = (near_right ? dr : dl);
            float d_v = (near_top ? dt : db);
            if (d_h < d_v) s->vx = -s->vx;   /* wall had the shorter run */
            else           s->vy = -s->vy;
            (*corners)++;
        }
    }

    if (s->x < l) { s->x = l; s->vx = -s->vx; bits |= 1; }
    else if (s->x > r) { s->x = r; s->vx = -s->vx; bits |= 1; }
    if (s->y < t) { s->y = t; s->vy = -s->vy; bits |= 2; }
    else if (s->y > b) { s->y = b; s->vy = -s->vy; bits |= 2; }
    return bits;
}

int anim_sprite_bounce(anim_sprite_t* s, float w, float h,
                       float guard, float dt_ms, int* corners) {
    /* Fixed 8ms substeps: physics stays smooth even when the redraw rate
       drops or the panel is dragged around. */
    const float STEP = 8.0f;
    int n = (int)(dt_ms / STEP) + 1;
    float fdt = dt_ms / (float)n;
    int bits = 0;
    for (int i = 0; i < n; i++) {
        s->x += s->vx * fdt;
        s->y += s->vy * fdt;
        s->angle += s->spin * fdt;
        bits |= reflect_walls(s, w, h, guard, corners);
    }
    return bits;
}

/* ===================== Canvas ===================== */

int anim_canvas_new(anim_canvas_t* c, int w, int h) {
    if (!c || w <= 0 || h <= 0) return -1;
    uint32_t* p = (uint32_t*)kmalloc((size_t)w * (size_t)h * 4);
    if (!p) return -1;
    c->px = p; c->w = w; c->h = h;
    anim_canvas_clear(c, 0x000000);
    return 0;
}

void anim_canvas_free(anim_canvas_t* c) {
    if (c && c->px) { kfree(c->px); c->px = 0; }
}

void anim_canvas_clear(anim_canvas_t* c, uint32_t rgb) {
    if (!c || !c->px) return;
    /* rgb stored as-is; the back buffer conversion happens at present time,
       so store the native pixel value for the canvas works. */
    memset(c->px, 0, (size_t)c->w * c->h * 4);
    if (rgb != 0) {
        int n = c->w * c->h;
        for (int i = 0; i < n; i++) c->px[i] = rgb;
    }
}

void anim_canvas_fade(anim_canvas_t* c, float keep) {
    if (!c || !c->px || keep >= 1.0f) return;
    int n = c->w * c->h;
    for (int i = 0; i < n; i++) {
        uint32_t p = c->px[i];
        if (!p) continue;
        int r = (int)((p >> 16 & 0xFF) * keep);
        int g = (int)((p >> 8  & 0xFF) * keep);
        int b = (int)((p       & 0xFF) * keep);
        c->px[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

void anim_canvas_fill_rect(anim_canvas_t* c, int x, int y, int w, int h, uint32_t rgb) {
    if (!c || !c->px) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c->w) w = c->w - x;
    if (y + h > c->h) h = c->h - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        uint32_t* dst = c->px + (y + row) * c->w + x;
        for (int col = 0; col < w; col++) dst[col] = rgb;
    }
}

void anim_canvas_rect_outline(anim_canvas_t* c, int x, int y, int w, int h, int t, uint32_t rgb) {
    anim_canvas_fill_rect(c, x, y, w, t, rgb);
    anim_canvas_fill_rect(c, x, y + h - t, w, t, rgb);
    anim_canvas_fill_rect(c, x, y + t, t, h - 2 * t, rgb);
    anim_canvas_fill_rect(c, x + w - t, y + t, t, h - 2 * t, rgb);
}

void anim_canvas_fill_circle(anim_canvas_t* c, int cx, int cy, int r, uint32_t rgb) {
    if (!c || !c->px || r <= 0) return;
    int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= c->w) x1 = c->w - 1; if (y1 >= c->h) y1 = c->h - 1;
    int r2 = r * r;
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx;
            if (dx * dx + dy * dy <= r2) c->px[y * c->w + x] = rgb;
        }
    }
}

void anim_canvas_circle_outline(anim_canvas_t* c, int cx, int cy, int r, int t, uint32_t rgb) {
    if (!c || r <= 0) return;
    int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= c->w) x1 = c->w - 1; if (y1 >= c->h) y1 = c->h - 1;
    int r2 = r * r;
    int r1 = (r - t > 0) ? r - t : 0;
    int r1_2 = r1 * r1;
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        for (int x = x0; x <= x1; x++) {
            int d2 = (x - cx) * (x - cx) + dy * dy;
            if (d2 <= r2 && d2 >= r1_2) c->px[y * c->w + x] = rgb;
        }
    }
}

void anim_canvas_vgrad(anim_canvas_t* c, int x, int y, int w, int h,
                       uint32_t top, uint32_t bottom) {
    for (int row = 0; row < h; row++) {
        float t = (h > 1) ? (float)row / (float)(h - 1) : 1.0f;
        anim_canvas_fill_rect(c, x, y + row, w, 1, anim_lerp_rgb(top, bottom, t));
    }
}

/* Rotated + scaled blit. Inverse-maps each pixel of the sprite's rotated
 * bounding box back to sprite space (nearest neighbour). */
static void blit_rotated(anim_canvas_t* c, const uint32_t* spr, int sw, int sh,
                         float cx, float cy, float angle, float scale,
                         uint32_t tint, int do_tint) {
    if (!c || !c->px || !spr || sw <= 0 || sh <= 0 || scale <= 0.0f) return;
    float ca = (float)cos((double)angle), sa = (float)sin((double)angle);
    float hw = sw * 0.5f, hh = sh * 0.5f;

    /* Rotated AABB in canvas space. */
    float rx = (float)fabs(hw * ca) + (float)fabs(hh * sa);
    float ry = (float)fabs(hw * sa) + (float)fabs(hh * ca);
    rx *= scale; ry *= scale;

    int x0 = (int)(cx - rx); if (x0 < 0) x0 = 0;
    int y0 = (int)(cy - ry); if (y0 < 0) y0 = 0;
    int x1 = (int)(cx + rx) + 1; if (x1 > c->w) x1 = c->w;
    int y1 = (int)(cy + ry) + 1; if (y1 > c->h) y1 = c->h;

    float inv = 1.0f / scale;
    uint32_t tint_r = (tint >> 16) & 0xFF;
    uint32_t tint_g = (tint >> 8) & 0xFF;
    uint32_t tint_b = tint & 0xFF;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = (float)px - cx;
            float dy = (float)py - cy;
            /* Rotate back into sprite space. */
            float u = (dx * ca + dy * sa) * inv;
            float v = (-dx * sa + dy * ca) * inv;
            int sx = (int)(u + hw);
            int sy = (int)(v + hh);
            if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) continue;
            uint32_t p = spr[sy * sw + sx];
            if ((p >> 24) == 0) continue;             /* transparent      */
            if (do_tint) {
                uint32_t g = (p >> 16) & 0xFF;
                uint32_t rr = (g * (tint_r + 1)) >> 8;
                uint32_t gg = (g * (tint_g + 1)) >> 8;
                uint32_t bbv = (g * (tint_b + 1)) >> 8;
                c->px[py * c->w + px] = (rr << 16) | (gg << 8) | bbv;
            } else {
                c->px[py * c->w + px] = p;
            }
        }
    }
}

void anim_canvas_blit(anim_canvas_t* c, const uint32_t* spr, int sw, int sh,
                      float cx, float cy, float angle, float scale) {
    blit_rotated(c, spr, sw, sh, cx, cy, angle, scale, 0xFFFFFF, 0);
}

void anim_canvas_blit_tinted(anim_canvas_t* c, const uint32_t* spr, int sw, int sh,
                             float cx, float cy, float angle, float scale,
                             uint32_t tint) {
    blit_rotated(c, spr, sw, sh, cx, cy, angle, scale, tint, 1);
}

/* ===================== Polygon Primitives ===================== */

/* Edge-fill (flat scanline) triangle rasteriser. Handles arbitrary winding. */
void anim_canvas_fill_triangle(anim_canvas_t* c, float x0, float y0,
                               float x1, float y1, float x2, float y2,
                               uint32_t rgb) {
    if (!c || !c->px) return;
    float tx, ty;
    /* three-element sort so y0 <= y1 <= y2 */
#define SWAPF(p,q,r,s) { tx=p; p=q; q=tx; ty=r; r=s; s=ty; }
    if (y0 > y1) SWAPF(x0,x1,y0,y1);
    if (y1 > y2) SWAPF(x1,x2,y1,y2);
    if (y0 > y1) SWAPF(x0,x1,y0,y1);
#undef SWAPF

    float inv_long = (y2 - y0) != 0.0f ? (x2 - x0) / (y2 - y0) : 0.0f;

    int y, ys = (int)y0, ye = (int)y2;
    if (ys < 0) ys = 0;
    if (ye >= c->h) ye = c->h - 1;
    for (y = ys; y <= ye; y++) {
        float yn = (float)y;
        float x_long = x0 + (yn - y0) * inv_long;     /* point on long edge */
        float x_split;
        if (yn <= y1) {
            float inv_s = (y1 - y0) != 0.0f ? (x1 - x0) / (y1 - y0) : 0.0f;
            x_split = x0 + (yn - y0) * inv_s;
        } else {
            float inv_s = (y2 - y1) != 0.0f ? (x2 - x1) / (y2 - y1) : 0.0f;
            x_split = x1 + (yn - y1) * inv_s;
        }
        float xa = x_long, xb = x_split;
        if (xa > xb) { float tt=xa; xa=xb; xb=tt; }
        int xs = (int)xa; if (xs < 0) xs = 0;
        int xe = (int)xb; if (xe >= c->w) xe = c->w - 1;
        if (xe < 0 || xs >= c->w || xe < xs) continue;
        uint32_t* row = c->px + (size_t)y * c->w;
        for (int xx = xs; xx <= xe; xx++) row[xx] = rgb;
    }
}

void anim_canvas_line(anim_canvas_t* c, float x0, float y0, float x1, float y1,
                      int t, uint32_t rgb) {
    if (!c || !c->px || t <= 0) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = (float)sqrt((double)(dx*dx + dy*dy));
    if (len <= 0.0f) return;
    dx /= len; dy /= len;
    float half = (float)(t) * 0.5f;
    int steps = (int)len + 1;
    for (int i = 0; i <= steps; i++) {
        float tt = (i < steps) ? (float)i / (float)steps : 1.0f;
        float cx = x0 + dx * tt * len;
        float cy = y0 + dy * tt * len;
        float px = -dy * half, py = dx * half;
        int x = (int)(cx + px + 0.5f);
        int y = (int)(cy + py + 0.5f);
        if (x >= 0 && y >= 0 && x < c->w && y < c->h)
            c->px[(size_t)y * c->w + x] = rgb;
    }
}

void anim_canvas_ellipse_outline(anim_canvas_t* c, float cx, float cy,
                                 float rx, float ry, int t, uint32_t rgb) {
    if (!c || !c->px || rx <= 0.0f || ry <= 0.0f || t <= 0) return;
    /* sample enough points for a smooth ring (step ~ 1px along the larger axis) */
    float perim = 6.2831853f * (rx + ry) * 0.5f;
    int steps = (int)perim + 1;
    if (steps < 48) steps = 48;
    if (steps > 512) steps = 512;
    float inv = 1.0f / (float)steps;
    for (int i = 0; i < steps; i++) {
        float a0 = 6.2831853f * (float)i * inv;
        float a1 = 6.2831853f * (float)(i + 1) * inv;
        float x0 = cx + (float)cos((double)a0) * rx;
        float y0 = cy + (float)sin((double)a0) * ry;
        float x1 = cx + (float)cos((double)a1) * rx;
        float y1 = cy + (float)sin((double)a1) * ry;
        anim_canvas_line(c, x0, y0, x1, y1, t, rgb);
    }
}

void anim_canvas_glow_disk(anim_canvas_t* c, float cx, float cy, int r,
                           uint32_t core_rgb, uint32_t glow_rgb) {
    if (!c || !c->px || r <= 0) return;
    for (int s = r + 6; s >= 0; s--) {
        float t = (r > 0) ? 1.0f - (float)s / (float)(r + 7) : 1.0f;
        uint32_t col = (s <= r) ? core_rgb
                                : anim_lerp_rgb(core_rgb, glow_rgb, t);
        anim_canvas_fill_circle(c, (int)(cx + 0.5f), (int)(cy + 0.5f), s, col);
    }
    /* tight white core */
    anim_canvas_fill_circle(c, (int)(cx + 0.5f), (int)(cy + 0.5f), r / 3, 0xFFFFFF);
}

void anim_canvas_present(anim_canvas_t* c, int dx, int dy, int dw, int dh) {
    gfx_draw_sprite_scaled(dx, dy, dw, dh, c->px, c->w, c->h);
}