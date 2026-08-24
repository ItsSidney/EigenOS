/***************************************************************/
/*  EigenUI Canvas — Evas-like 2D surface (EigenUI Canvas)      */
/*                                                             */
/*  Draws into a flat 0x00RRGGBB uint32 buffer (the window     */
/*  content buffer). All coordinates are LOGICAL; a scale       */
/*  factor (DPI / rescaling) is applied automatically. Clip     */
/*  rectangles are stacked. Modern shading (rounded corners,   */
/*  gradients, soft shadows) is built in.                       */
/***************************************************************/
#ifndef EUI_CANVAS_H
#define EUI_CANVAS_H

#include "eui_core.h"

typedef struct eui_canvas {
    uint32_t* buf;        /* device-pixel framebuffer (0x00RRGGBB)   */
    int       w, h;       /* device dimensions                       */
    float     scale;      /* logical->device multiplier (rescaling)  */
    eui_rect  clip;       /* current clip, device px                 */
    eui_rect  clip_stack[16];
    int       clip_sp;
} eui_canvas;

void eui_canvas_init(eui_canvas* c, uint32_t* buf, int w, int h);
void eui_canvas_set_scale(eui_canvas* c, float s);
void eui_canvas_push_clip(eui_canvas* c, const eui_rect* r);
void eui_canvas_pop_clip(eui_canvas* c);

void eui_canvas_clear(eui_canvas* c, eui_color col);
void eui_canvas_pixel(eui_canvas* c, int x, int y, eui_color col);

/* Filled rectangle (alpha 0..255). */
void eui_canvas_fill_rect(eui_canvas* c, int x, int y, int w, int h, eui_color col);
void eui_canvas_fill_rect_a(eui_canvas* c, int x, int y, int w, int h, eui_color col, uint8_t a);

/* Rounded filled rectangle with anti-aliased corners. */
void eui_canvas_round(eui_canvas* c, int x, int y, int w, int h, int r, eui_color col);
void eui_canvas_round_a(eui_canvas* c, int x, int y, int w, int h, int r, eui_color col, uint8_t a);

/* Rounded rectangle outline (1px logical, AA). */
void eui_canvas_round_stroke(eui_canvas* c, int x, int y, int w, int h, int r, int t, eui_color col);

/* Linear gradients across a (optionally rounded) rect. */
void eui_canvas_vgrad(eui_canvas* c, int x, int y, int w, int h, int r, eui_color top, eui_color bot);
void eui_canvas_hgrad(eui_canvas* c, int x, int y, int w, int h, int r, eui_color l, eui_color rt);

/* Soft drop shadow behind a rounded rect (offset down-right by (ox,oy)). */
void eui_canvas_shadow(eui_canvas* c, int x, int y, int w, int h, int r, int blur, eui_color col);

/* Straight line (1px logical). */
void eui_canvas_line(eui_canvas* c, int x0, int y0, int x1, int y1, eui_color col);

/* Blit an ARGB8888 source (0xAARRGGBB) into the canvas, scaled to dw x dh.
   Used by icons / images. Alpha in the source is honoured. */
void eui_canvas_blit(eui_canvas* c, const uint32_t* src, int sw, int sh,
                     int dx, int dy, int dw, int dh);

/* Custom vector shapes (scalable building blocks for bespoke widgets). */
void eui_canvas_triangle(eui_canvas* c, int x0,int y0,int x1,int y1,int x2,int y2, eui_color col);
void eui_canvas_triangle_a(eui_canvas* c, int x0,int y0,int x1,int y1,int x2,int y2, eui_color col, uint8_t a);
/* Even-odd scanline fill of an arbitrary polygon (pts in logical coords). */
void eui_canvas_polygon(eui_canvas* c, const eui_point* pts, int n, eui_color col);
void eui_canvas_polygon_a(eui_canvas* c, const eui_point* pts, int n, eui_color col, uint8_t a);

#endif /* EUI_CANVAS_H */
