/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* fractal2d.h — shared 2D escape-time fractal renderer.
 * Software, no SIMD (kernel is built -mno-sse/-mno-mmx): uses
 * fixed float math + smooth log-based coloring.
 */
#ifndef _LIBS_FRACTAL2D_H
#define _LIBS_FRACTAL2D_H

#include <stdint.h>

#define F2D_MAX_PAL_STOPS 8

typedef struct {
    const char* name;
    uint32_t colors[F2D_MAX_PAL_STOPS];
    int n;
} f2d_palette_t;

extern const f2d_palette_t f2d_palettes[4];
int  f2d_palette_count(void);

/* t in [0,1] — cyclic smooth palette lookup (wraps, no banding). */
uint32_t f2d_color(int pal_id, float t);

/* log2(x) for x > 0, no libm (frexp-style bit extraction + polynomial). */
float f2d_log2_approx(float x);

/* Render a full frame into buf (w*h uint32s, bottom-up rows).
 * scale = complex-plane width visible across the view. */
void f2d_render_mandelbrot(uint32_t* buf, int w, int h,
                           float cx, float cy, float scale,
                           int iters, int pal);
void f2d_render_julia(uint32_t* buf, int w, int h,
                      float cr, float ci, float scale,
                      int iters, int pal);

#endif