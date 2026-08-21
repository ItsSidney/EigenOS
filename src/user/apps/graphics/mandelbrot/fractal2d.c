/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "libs/fractal2d.h"

/* ── Palettes ──────────────────────────────────────────────────
 * 8-stop ramps designed to wrap: f2d_color() lerps mod n, so the
 * last stop blends back into the first — bandless continuous
 * gradients instead of hard clamps. */
const f2d_palette_t f2d_palettes[4] = {
    { "Aurora", { 0x050816, 0x16213E, 0x0F4C75, 0x3282B8,
                  0x4DABF7, 0x7CF5C0, 0xF9C74F, 0xE56B6F }, 8 },
    { "Plasma", { 0x0D0221, 0x3A0CA3, 0x7209B7, 0x9D4EDD,
                  0xEC4899, 0xF72585, 0xF77F00, 0xFCA311 }, 8 },
    { "Emerald", { 0x022C22, 0x0D3B2E, 0x0A6C54, 0x0FA47B,
                   0x2DD4BF, 0x99F6E4, 0xE0FBF4, 0x4ADE80 }, 8 },
    { "Ember", { 0x1A0505, 0x450A0A, 0x9D0208, 0xDC2F02,
                 0xF48C06, 0xF9C74F, 0xFFE8A3, 0xFF5D5D }, 8 },
};

int f2d_palette_count(void) { return 4; }

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
    int r = (int)(((a >> 16) & 0xFF) + (((b >> 16) & 0xFF) - ((a >> 16) & 0xFF)) * t);
    int g = (int)(((a >> 8) & 0xFF) + (((b >> 8) & 0xFF) - ((a >> 8) & 0xFF)) * t);
    int bv = (int)((a & 0xFF) + ((b & 0xFF) - (a & 0xFF)) * t);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (bv < 0) bv = 0; if (bv > 255) bv = 255;
    return (r << 16) | (g << 8) | bv;
}

uint32_t f2d_color(int pal_id, float t) {
    if (t >= 1.0f) return 0x000000;           /* interior: solid black */
    const f2d_palette_t* p = &f2d_palettes[pal_id % 4];
    float pos = t * (float)p->n;               /* [0, n) */
    int i = (int)pos;
    float f = pos - (float)i;
    return lerp_rgb(p->colors[i % p->n], p->colors[(i + 1) % p->n], f);
}

/* log2 via float-bit exponent + minimax poly on the mantissa.
 * Accurate to ~1e-3, plenty for smooth coloring, no libm needed. */
float f2d_log2_approx(float x) {
    union { float f; uint32_t u; } c;
    c.f = x;
    int e = (int)((c.u >> 23) & 0xFF) - 127;
    float m = (float)(c.u & 0x7FFFFF) / 8388608.0f;   /* [0,1) */
    float t = m;
    float p = t * (1.4426950408889634f +
              t * (-0.7213475204444817f +
              t * (0.4808983469629878f +
              t * (-0.3606742600511409f +
              t * 0.2885394080409127f))));
    return (float)e + p;
}

/* Escape-time core shared by both sets. `mode`: 0 = mandelbrot (z0=0,
 * c=pixel), 1 = julia (z0=pixel, c=const). Returns 0..iters and leaves
 * the final z in *ozx/*ozy so callers can smooth-color without re-running. */
static int escape_loop(float x, float y, float cr, float ci,
                       int iters, int mode, float* ozx, float* ozy) {
    float zx = mode ? x : 0.0f;
    float zy = mode ? y : 0.0f;
    float ccx = mode ? cr : x;
    float ccy = mode ? ci : y;
    int iter = 0;
    while (zx * zx + zy * zy <= 4.0f && iter < iters) {
        float xt = zx * zx - zy * zy + ccx;
        zy = 2.0f * zx * zy + ccy;
        zx = xt;
        iter++;
    }
    if (ozx) *ozx = zx;
    if (ozy) *ozy = zy;
    return iter;
}

static void render_common(uint32_t* buf, int w, int h,
                          float cx, float cy, float scale,
                          float jr, float ji, int iters, int pal, int mode) {
    for (int py = 0; py < h; py++) {
        float y0 = (py - h / 2.0f) * scale / h + cy;
        for (int px = 0; px < w; px++) {
            float x0 = (px - w / 2.0f) * scale / w + cx;
            float zx, zy;
            int iter = escape_loop(x0, y0, jr, ji, iters, mode, &zx, &zy);
            float t;
            if (iter >= iters) {
                t = 1.0f;
            } else {
                /* smooth coloring: mu = n + 1 - log2(ln|z|).
                 * log2(ln|z|) = log2(log2|z|) - 1.5287. */
                float zmag = zx * zx + zy * zy;
                float smooth = (float)iter + 2.5287f -
                               f2d_log2_approx(f2d_log2_approx(zmag));
                t = smooth / (float)iters;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
            }
            buf[py * w + px] = f2d_color(pal, t);
        }
    }
}

void f2d_render_mandelbrot(uint32_t* buf, int w, int h,
                           float cx, float cy, float scale,
                           int iters, int pal) {
    render_common(buf, w, h, cx, cy, scale, 0.0f, 0.0f, iters, pal, 0);
}

void f2d_render_julia(uint32_t* buf, int w, int h,
                      float cr, float ci, float scale,
                      int iters, int pal) {
    render_common(buf, w, h, 0.0f, 0.0f, scale, cr, ci, iters, pal, 1);
}