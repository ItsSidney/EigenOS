/***************************************************************/
/*  EigenUI Canvas — implementation                             */
/***************************************************************/
#include "eui_canvas.h"
#include <string.h>

/* local float math (no libm dependency) */
static float eui_fabsf(float x) { return x < 0 ? -x : x; }
static float eui_sqrtf(float x) {
    if (x <= 0) return 0;
    float g = x * 0.5f;
    for (int i = 0; i < 6; i++) { g = 0.5f * (g + x / g); }
    return g;
}

static inline int SX(eui_canvas* c, int v) { return (int)((float)v * c->scale + 0.5f); }

void eui_canvas_init(eui_canvas* c, uint32_t* buf, int w, int h) {
    c->buf = buf; c->w = w; c->h = h; c->scale = 1.0f;
    c->clip = EUI_RECT(0, 0, w, h);
    c->clip_sp = 0;
}
void eui_canvas_set_scale(eui_canvas* c, float s) { if (s > 0) c->scale = s; }

void eui_canvas_push_clip(eui_canvas* c, const eui_rect* r) {
    if (c->clip_sp >= 16) return;
    c->clip_stack[c->clip_sp++] = c->clip;
    eui_rect d = { SX(c, r->x), SX(c, r->y), SX(c, r->w), SX(c, r->h) };
    eui_rect n;
    n.x = eui_max_i(c->clip.x, d.x);
    n.y = eui_max_i(c->clip.y, d.y);
    n.w = eui_min_i(c->clip.x + c->clip.w, d.x + d.w) - n.x;
    n.h = eui_min_i(c->clip.y + c->clip.h, d.y + d.h) - n.y;
    if (n.w < 0) n.w = 0; if (n.h < 0) n.h = 0;
    c->clip = n;
}
void eui_canvas_pop_clip(eui_canvas* c) {
    if (c->clip_sp <= 0) return;
    c->clip = c->clip_stack[--c->clip_sp];
}

static inline void px(eui_canvas* c, int x, int y, eui_color col) {
    if (x < c->clip.x || y < c->clip.y || x >= c->clip.x + c->clip.w || y >= c->clip.y + c->clip.h)
        return;
    c->buf[y * c->w + x] = col;
}

void eui_canvas_clear(eui_canvas* c, eui_color col) {
    for (int y = c->clip.y; y < c->clip.y + c->clip.h; y++)
        for (int x = c->clip.x; x < c->clip.x + c->clip.w; x++)
            c->buf[y * c->w + x] = col;
}
void eui_canvas_pixel(eui_canvas* c, int x, int y, eui_color col) {
    px(c, SX(c, x), SX(c, y), col);
}
void eui_canvas_fill_rect_a(eui_canvas* c, int x, int y, int w, int h, eui_color col, uint8_t a) {
    int X = SX(c, x), Y = SX(c, y), W = SX(c, w), H = SX(c, h);
    for (int yy = Y; yy < Y + H; yy++)
        for (int xx = X; xx < X + W; xx++) {
            if (xx < c->clip.x || yy < c->clip.y || xx >= c->clip.x + c->clip.w || yy >= c->clip.y + c->clip.h)
                continue;
            c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], col, a);
        }
}
void eui_canvas_fill_rect(eui_canvas* c, int x, int y, int w, int h, eui_color col) {
    eui_canvas_fill_rect_a(c, x, y, w, h, col, 255);
}

/* Signed distance to a rounded box centred at (cx,cy) with half-extents hw,hh, radius r. */
static float sd_round(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = eui_fabsf(px - cx) - hw + r;
    float qy = eui_fabsf(py - cy) - hh + r;
    float ax = eui_fabsf(qx), ay = eui_fabsf(qy);
    float out = eui_sqrtf(ax * ax + ay * ay) + eui_min_i(qx > qy ? qy : qx, 0) - r;
    return out;
}

void eui_canvas_round_a(eui_canvas* c, int x, int y, int w, int h, int r, eui_color col, uint8_t a) {
    if (w <= 0 || h <= 0) return;
    int X = SX(c, x), Y = SX(c, y), W = SX(c, w), H = SX(c, h);
    int rr = SX(c, r); if (rr > W / 2) rr = W / 2; if (rr > H / 2) rr = H / 2;
    if (rr < 0) rr = 0;
    int cx = X + W / 2, cy = Y + H / 2;
    int x0 = eui_max_i(X, c->clip.x), x1 = eui_min_i(X + W, c->clip.x + c->clip.w);
    int y0 = eui_max_i(Y, c->clip.y), y1 = eui_min_i(Y + H, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float d = sd_round((float)xx + 0.5f, (float)yy + 0.5f, (float)cx, (float)cy,
                               (float)W / 2, (float)H / 2, (float)rr);
            int cov = (int)(255.0f * (0.5f - d) + 0.5f);
            if (cov <= 0) continue;
            if (cov > 255) cov = 255;
            c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], col, (uint8_t)((a * cov) / 255));
        }
}
void eui_canvas_round(eui_canvas* c, int x, int y, int w, int h, int r, eui_color col) {
    eui_canvas_round_a(c, x, y, w, h, r, col, 255);
}

void eui_canvas_round_stroke(eui_canvas* c, int x, int y, int w, int h, int r, int t, eui_color col) {
    if (t <= 0) return;
    int X = SX(c, x), Y = SX(c, y), W = SX(c, w), H = SX(c, h);
    int rr = SX(c, r); if (rr > W / 2) rr = W / 2; if (rr > H / 2) rr = H / 2;
    if (rr < 0) rr = 0;
    int tt = SX(c, t); if (tt < 1) tt = 1;
    int cx = X + W / 2, cy = Y + H / 2;
    int x0 = eui_max_i(X, c->clip.x), x1 = eui_min_i(X + W, c->clip.x + c->clip.w);
    int y0 = eui_max_i(Y, c->clip.y), y1 = eui_min_i(Y + H, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float d = sd_round((float)xx + 0.5f, (float)yy + 0.5f, (float)cx, (float)cy,
                               (float)W / 2, (float)H / 2, (float)rr);
            if (d >= 0 && d < (float)tt) {
                int cov = (int)(255.0f * (1.0f - (d / (float)tt)) + 0.5f);
                if (cov > 255) cov = 255;
                c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], col, (uint8_t)cov);
            }
        }
}

void eui_canvas_vgrad(eui_canvas* c, int x, int y, int w, int h, int r, eui_color top, eui_color bot) {
    if (w <= 0 || h <= 0) return;
    int X = SX(c, x), Y = SX(c, y), W = SX(c, w), H = SX(c, h);
    int rr = SX(c, r); if (rr > W / 2) rr = W / 2; if (rr > H / 2) rr = H / 2;
    if (rr < 0) rr = 0;
    int cx = X + W / 2, cy = Y + H / 2;
    int x0 = eui_max_i(X, c->clip.x), x1 = eui_min_i(X + W, c->clip.x + c->clip.w);
    int y0 = eui_max_i(Y, c->clip.y), y1 = eui_min_i(Y + H, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float d = sd_round((float)xx + 0.5f, (float)yy + 0.5f, (float)cx, (float)cy,
                               (float)W / 2, (float)H / 2, (float)rr);
            if (d > 0.5f) continue;
            int cov = 255 - (int)(255.0f * (0.5f - d) + 0.5f);
            if (cov < 0) cov = 0;
            int t = H <= 1 ? 0 : ((yy - Y) * 255) / H;
            eui_color col = eui_lerp(top, bot, t, 255);
            c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], col, (uint8_t)cov);
        }
}
void eui_canvas_hgrad(eui_canvas* c, int x, int y, int w, int h, int r, eui_color l, eui_color rt) {
    if (w <= 0 || h <= 0) return;
    int X = SX(c, x), Y = SX(c, y), W = SX(c, w), H = SX(c, h);
    int rr = SX(c, r); if (rr > W / 2) rr = W / 2; if (rr > H / 2) rr = H / 2;
    if (rr < 0) rr = 0;
    int cx = X + W / 2, cy = Y + H / 2;
    int x0 = eui_max_i(X, c->clip.x), x1 = eui_min_i(X + W, c->clip.x + c->clip.w);
    int y0 = eui_max_i(Y, c->clip.y), y1 = eui_min_i(Y + H, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float d = sd_round((float)xx + 0.5f, (float)yy + 0.5f, (float)cx, (float)cy,
                               (float)W / 2, (float)H / 2, (float)rr);
            if (d > 0.5f) continue;
            int cov = 255 - (int)(255.0f * (0.5f - d) + 0.5f);
            if (cov < 0) cov = 0;
            int t = W <= 1 ? 0 : ((xx - X) * 255) / W;
            eui_color col = eui_lerp(l, rt, t, 255);
            c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], col, (uint8_t)cov);
        }
}

void eui_canvas_shadow(eui_canvas* c, int x, int y, int w, int h, int r, int blur, eui_color col) {
    if (blur <= 0) return;
    int X = SX(c, x), Y = SX(c, y), W = SX(c, w), H = SX(c, h);
    int rr = SX(c, r); if (rr > W / 2) rr = W / 2; if (rr > H / 2) rr = H / 2;
    if (rr < 0) rr = 0;
    int bb = SX(c, blur);
    int cx = X + W / 2, cy = Y + H / 2;
    int bx0 = eui_max_i(X - bb, c->clip.x), bx1 = eui_min_i(X + W + bb, c->clip.x + c->clip.w);
    int by0 = eui_max_i(Y - bb, c->clip.y), by1 = eui_min_i(Y + H + bb, c->clip.y + c->clip.h);
    for (int yy = by0; yy < by1; yy++)
        for (int xx = bx0; xx < bx1; xx++) {
            float d = sd_round((float)xx + 0.5f, (float)yy + 0.5f, (float)cx, (float)cy,
                               (float)W / 2, (float)H / 2, (float)rr);
            if (d > (float)bb) continue;
            int a = (int)(200.0f * (1.0f - d / (float)bb));
            if (a <= 0) continue; if (a > 200) a = 200;
            c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], col, (uint8_t)a);
        }
}

void eui_canvas_line(eui_canvas* c, int x0, int y0, int x1, int y1, eui_color col) {
    int X0 = SX(c, x0), Y0 = SX(c, y0), X1 = SX(c, x1), Y1 = SX(c, y1);
    int dx = eui_fabsf((float)(X1 - X0)) > 0 ? (X1 - X0) : 0;
    int dy = Y1 - Y0;
    int steps = eui_max_i(eui_fabsf((float)dx) > eui_fabsf((float)dy) ? eui_fabsf((float)dx) : eui_fabsf((float)dy), 1);
    for (int i = 0; i <= steps; i++) {
        int xx = X0 + (X1 - X0) * i / steps;
        int yy = Y0 + (Y1 - Y0) * i / steps;
        px(c, xx, yy, col);
    }
}

void eui_canvas_blit(eui_canvas* c, const uint32_t* src, int sw, int sh,
                     int dx, int dy, int dw, int dh) {
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    int X = SX(c, dx), Y = SX(c, dy), W = SX(c, dw), H = SX(c, dh);
    int x0 = eui_max_i(X, c->clip.x), x1 = eui_min_i(X + W, c->clip.x + c->clip.w);
    int y0 = eui_max_i(Y, c->clip.y), y1 = eui_min_i(Y + H, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            int sx = (xx - X) * sw / W;
            int sy = (yy - Y) * sh / H;
            if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) continue;
            uint32_t s = src[sy * sw + sx];
            uint8_t a = (s >> 24) & 0xFF;
            if (!a) continue;
            c->buf[yy * c->w + xx] = eui_blend(c->buf[yy * c->w + xx], s & 0x00FFFFFF, a);
        }
}

/* ── custom shapes ────────────────────────────────────────────── */
static void poly_fill(eui_canvas* c, const eui_point* pts, int n, eui_color col, uint8_t a) {
    if (n < 3) return;
    int miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].y < miny) miny = pts[i].y;
        if (pts[i].y > maxy) maxy = pts[i].y;
    }
    int Y0 = eui_max_i((int)((float)miny * c->scale + 0.5f), c->clip.y);
    int Y1 = eui_min_i((int)((float)maxy * c->scale + 0.5f) + 1, c->clip.y + c->clip.h);
    float* xs = (float*)eigen_malloc(sizeof(float) * n);
    for (int y = Y0; y < Y1; y++) {
        float fy = ((float)y + 0.5f) / c->scale;
        int k = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float yi = pts[i].y, yj = pts[j].y;
            if ((yi > fy) != (yj > fy)) {
                xs[k++] = pts[i].x + (fy - yi) / (yj - yi) * (pts[j].x - pts[i].x);
            }
        }
        for (int i = 0; i < k - 1; i++)
            for (int j = i + 1; j < k; j++)
                if (xs[j] < xs[i]) { float t = xs[i]; xs[i] = xs[j]; xs[j] = t; }
        for (int i = 0; i + 1 < k; i += 2) {
            int xa = eui_max_i((int)((float)xs[i] * c->scale + 0.5f), c->clip.x);
            int xb = eui_min_i((int)((float)xs[i + 1] * c->scale + 0.5f), c->clip.x + c->clip.w);
            for (int x = xa; x < xb; x++)
                c->buf[y * c->w + x] = eui_blend(c->buf[y * c->w + x], col, a);
        }
    }
    eigen_free(xs);
}
void eui_canvas_polygon(eui_canvas* c, const eui_point* pts, int n, eui_color col) {
    poly_fill(c, pts, n, col, 255);
}
void eui_canvas_polygon_a(eui_canvas* c, const eui_point* pts, int n, eui_color col, uint8_t a) {
    poly_fill(c, pts, n, col, a);
}
void eui_canvas_triangle(eui_canvas* c, int x0,int y0,int x1,int y1,int x2,int y2, eui_color col) {
    eui_point p[3] = { {x0,y0},{x1,y1},{x2,y2} };
    poly_fill(c, p, 3, col, 255);
}
void eui_canvas_triangle_a(eui_canvas* c, int x0,int y0,int x1,int y1,int x2,int y2, eui_color col, uint8_t a) {
    eui_point p[3] = { {x0,y0},{x1,y1},{x2,y2} };
    poly_fill(c, p, 3, col, a);
}
