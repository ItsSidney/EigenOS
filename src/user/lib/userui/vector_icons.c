/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "vector_icons.h"
#include <math.h>

#ifndef sinf
#define sinf(x) ((float)sin((double)(x)))
#endif
#ifndef cosf
#define cosf(x) ((float)cos((double)(x)))
#endif

#define PI 3.14159265358979323846f

/* ── Pixel blending helper ── */
static inline void set_px(uint32_t* buf, int w, int h, int x, int y, uint32_t rgb, int a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
    if (a >= 255) { buf[y * w + x] = rgb; return; }
    uint32_t bg = buf[y * w + x];
    int sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    int dr = (bg >> 16) & 0xFF, dg = (bg >> 8) & 0xFF, db = bg & 0xFF;
    int r = (sr * a + dr * (255 - a)) / 255;
    int g = (sg * a + dg * (255 - a)) / 255;
    int b = (sb * a + db * (255 - a)) / 255;
    buf[y * w + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void draw_vector_disc(uint32_t* buf, int buf_w, int buf_h,
                      int cx, int cy, int radius, uint32_t color) {
    if (radius <= 0) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= buf_h) continue;
        int dy2 = dy * dy;
        for (int dx = -radius; dx <= radius; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= buf_w) continue;
            if (dx * dx + dy2 <= r2) {
                buf[py * buf_w + px] = color;
            }
        }
    }
}

void draw_vector_line(uint32_t* buf, int buf_w, int buf_h,
                      int x0, int y0, int x1, int y1,
                      int stroke, uint32_t color) {
    if (stroke < 1) stroke = 1;
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps == 0) {
        draw_vector_disc(buf, buf_w, buf_h, x0, y0, stroke / 2 + 1, color);
        return;
    }
    float xinc = (float)dx / (float)steps;
    float yinc = (float)dy / (float)steps;
    float cx = (float)x0;
    float cy = (float)y0;
    int hr = stroke / 2;
    for (int i = 0; i <= steps; i++) {
        if (stroke == 1) {
            set_px(buf, buf_w, buf_h, (int)(cx + 0.5f), (int)(cy + 0.5f), color, 255);
        } else {
            draw_vector_disc(buf, buf_w, buf_h, (int)(cx + 0.5f), (int)(cy + 0.5f), hr, color);
        }
        cx += xinc;
        cy += yinc;
    }
}

void draw_vector_circle(uint32_t* buf, int buf_w, int buf_h,
                        int cx, int cy, int radius,
                        int stroke, uint32_t color) {
    if (radius <= 0) return;
    if (stroke < 1) stroke = 1;
    int steps = (int)(2.0f * PI * (float)radius * 1.4f);
    if (steps < 16) steps = 16;
    float step_rad = (2.0f * PI) / (float)steps;
    for (int i = 0; i < steps; i++) {
        float a = (float)i * step_rad;
        int px = cx + (int)(cosf(a) * (float)radius + 0.5f);
        int py = cy + (int)(sinf(a) * (float)radius + 0.5f);
        if (stroke == 1) {
            set_px(buf, buf_w, buf_h, px, py, color, 255);
        } else {
            draw_vector_disc(buf, buf_w, buf_h, px, py, stroke / 2, color);
        }
    }
}

void draw_vector_arc(uint32_t* buf, int buf_w, int buf_h,
                     int cx, int cy, int radius,
                     int start_deg, int end_deg,
                     int stroke, uint32_t color) {
    if (radius <= 0) return;
    if (stroke < 1) stroke = 1;
    while (end_deg < start_deg) end_deg += 360;
    int span = end_deg - start_deg;
    int steps = (int)((float)span * (float)radius * 0.035f);
    if (steps < 8) steps = 8;
    float rad_start = (float)start_deg * PI / 180.0f;
    float rad_span = (float)span * PI / 180.0f;
    for (int i = 0; i <= steps; i++) {
        float a = rad_start + rad_span * ((float)i / (float)steps);
        int px = cx + (int)(cosf(a) * (float)radius + 0.5f);
        int py = cy + (int)(sinf(a) * (float)radius + 0.5f);
        draw_vector_disc(buf, buf_w, buf_h, px, py, stroke / 2, color);
    }
}

void draw_vector_round_rect(uint32_t* buf, int buf_w, int buf_h,
                            int x, int y, int w, int h, int r,
                            int stroke, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) {
        draw_vector_line(buf, buf_w, buf_h, x, y, x + w, y, stroke, color);
        draw_vector_line(buf, buf_w, buf_h, x + w, y, x + w, y + h, stroke, color);
        draw_vector_line(buf, buf_w, buf_h, x + w, y + h, x, y + h, stroke, color);
        draw_vector_line(buf, buf_w, buf_h, x, y + h, x, y, stroke, color);
        return;
    }
    draw_vector_line(buf, buf_w, buf_h, x + r, y, x + w - r, y, stroke, color);
    draw_vector_line(buf, buf_w, buf_h, x + w, y + r, x + w, y + h - r, stroke, color);
    draw_vector_line(buf, buf_w, buf_h, x + w - r, y + h, x + r, y + h, stroke, color);
    draw_vector_line(buf, buf_w, buf_h, x, y + h - r, x, y + r, stroke, color);
    draw_vector_arc(buf, buf_w, buf_h, x + w - r, y + r, r, 270, 360, stroke, color);
    draw_vector_arc(buf, buf_w, buf_h, x + w - r, y + h - r, r, 0, 90, stroke, color);
    draw_vector_arc(buf, buf_w, buf_h, x + r, y + h - r, r, 90, 180, stroke, color);
    draw_vector_arc(buf, buf_w, buf_h, x + r, y + r, r, 180, 270, stroke, color);
}

/* ── Lucide / Feather Vector Icon Rasterizer ── */
void draw_vector_icon(uint32_t* buf, int buf_w, int buf_h,
                      int x, int y, int sz,
                      vector_icon_id_t icon_id, uint32_t color) {
    if (sz < 8) sz = 8;
    int stroke = (sz >= 32) ? 2 : (sz >= 20 ? 2 : 1);
    float s = (float)sz / 24.0f; /* normalized 24x24 coordinate base */
#define P(u, v) (x + (int)((u) * s + 0.5f)), (y + (int)((v) * s + 0.5f))

    switch (icon_id) {
    case ICON_CALCULATOR: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(4*s), y + (int)(2*s), (int)(16*s), (int)(20*s), (int)(3*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(8, 6), P(16, 6), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(8, 10), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(12, 10), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(16, 10), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(8, 14), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(12, 14), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(16, 14), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(8, 18), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(12, 18), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(15, 18), P(17, 18), stroke, color);
        break;
    }
    case ICON_CLOCK: {
        draw_vector_circle(buf, buf_w, buf_h, P(12, 12), (int)(10*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 12), P(12, 6), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 12), P(16, 14), stroke, color);
        break;
    }
    case ICON_SETTINGS: {
        draw_vector_circle(buf, buf_w, buf_h, P(12, 12), (int)(4*s), stroke, color);
        for (int i = 0; i < 6; i++) {
            float rad = (float)i * (PI / 3.0f);
            int x1 = x + (int)((12.0f + 6.0f * cosf(rad)) * s);
            int y1 = y + (int)((12.0f + 6.0f * sinf(rad)) * s);
            int x2 = x + (int)((12.0f + 9.5f * cosf(rad)) * s);
            int y2 = y + (int)((12.0f + 9.5f * sinf(rad)) * s);
            draw_vector_line(buf, buf_w, buf_h, x1, y1, x2, y2, stroke + 1, color);
        }
        break;
    }
    case ICON_CALENDAR: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(3*s), y + (int)(4*s), (int)(18*s), (int)(17*s), (int)(2*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(16, 2), P(16, 6), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(8, 2), P(8, 6), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 10), P(21, 10), stroke, color);
        break;
    }
    case ICON_PLAY: {
        draw_vector_line(buf, buf_w, buf_h, P(6, 4), P(19, 12), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(19, 12), P(6, 20), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(6, 20), P(6, 4), stroke, color);
        break;
    }
    case ICON_PAUSE: {
        draw_vector_line(buf, buf_w, buf_h, P(7, 4), P(7, 20), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(17, 4), P(17, 20), stroke + 1, color);
        break;
    }
    case ICON_ROTATE_CCW: {
        draw_vector_arc(buf, buf_w, buf_h, P(12, 12), (int)(8*s), 45, 315, stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(4, 8), P(4, 14), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(4, 8), P(10, 8), stroke, color);
        break;
    }
    case ICON_FLAG: {
        draw_vector_line(buf, buf_w, buf_h, P(4, 2), P(4, 22), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(4, 4), P(18, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(18, 9), P(4, 14), stroke, color);
        break;
    }
    case ICON_BELL: {
        draw_vector_arc(buf, buf_w, buf_h, P(12, 9), (int)(6*s), 180, 360, stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(6, 9), P(4, 17), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(18, 9), P(20, 17), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 17), P(21, 17), stroke, color);
        draw_vector_arc(buf, buf_w, buf_h, P(12, 17), (int)(3*s), 0, 180, stroke, color);
        break;
    }
    case ICON_CHECK: {
        draw_vector_line(buf, buf_w, buf_h, P(4, 12), P(9, 18), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(9, 18), P(20, 6), stroke + 1, color);
        break;
    }
    case ICON_X: {
        draw_vector_line(buf, buf_w, buf_h, P(5, 5), P(19, 19), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(19, 5), P(5, 19), stroke, color);
        break;
    }
    case ICON_PLUS: {
        draw_vector_line(buf, buf_w, buf_h, P(12, 5), P(12, 19), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(5, 12), P(19, 12), stroke + 1, color);
        break;
    }
    case ICON_MINUS: {
        draw_vector_line(buf, buf_w, buf_h, P(5, 12), P(19, 12), stroke + 1, color);
        break;
    }
    case ICON_DIVIDE: {
        draw_vector_disc(buf, buf_w, buf_h, P(12, 6), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(5, 12), P(19, 12), stroke + 1, color);
        draw_vector_disc(buf, buf_w, buf_h, P(12, 18), stroke + 1, color);
        break;
    }
    case ICON_PERCENT: {
        draw_vector_line(buf, buf_w, buf_h, P(19, 5), P(5, 19), stroke, color);
        draw_vector_circle(buf, buf_w, buf_h, P(8, 8), (int)(2.5f*s), stroke, color);
        draw_vector_circle(buf, buf_w, buf_h, P(16, 16), (int)(2.5f*s), stroke, color);
        break;
    }
    case ICON_EQUAL: {
        draw_vector_line(buf, buf_w, buf_h, P(5, 9), P(19, 9), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(5, 15), P(19, 15), stroke + 1, color);
        break;
    }
    case ICON_BACKSPACE: {
        draw_vector_line(buf, buf_w, buf_h, P(9, 4), P(21, 4), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(21, 4), P(21, 20), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(21, 20), P(9, 20), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(9, 20), P(3, 12), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 12), P(9, 4), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 9), P(17, 15), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(17, 9), P(12, 15), stroke, color);
        break;
    }
    case ICON_SUN: {
        draw_vector_circle(buf, buf_w, buf_h, P(12, 12), (int)(5*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 2), P(12, 5), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 19), P(12, 22), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(2, 12), P(5, 12), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(19, 12), P(22, 12), stroke, color);
        break;
    }
    case ICON_MOON: {
        draw_vector_arc(buf, buf_w, buf_h, P(13, 12), (int)(8*s), 110, 290, stroke, color);
        draw_vector_arc(buf, buf_w, buf_h, P(10, 12), (int)(6*s), 70, 290, stroke, color);
        break;
    }
    case ICON_SEARCH: {
        draw_vector_circle(buf, buf_w, buf_h, P(10, 10), (int)(6*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(14, 14), P(20, 20), stroke + 1, color);
        break;
    }
    case ICON_PALETTE: {
        draw_vector_circle(buf, buf_w, buf_h, P(12, 12), (int)(9*s), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(7, 10), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(11, 7), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(16, 10), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(14, 15), stroke, color);
        break;
    }
    case ICON_IMAGE: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(3*s), y + (int)(3*s), (int)(18*s), (int)(18*s), (int)(3*s), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(8, 8), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(4, 18), P(10, 12), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(10, 12), P(15, 17), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(15, 17), P(20, 13), stroke, color);
        break;
    }
    case ICON_MONITOR: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(2*s), y + (int)(3*s), (int)(20*s), (int)(14*s), (int)(2*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(8, 21), P(16, 21), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 17), P(12, 21), stroke, color);
        break;
    }
    case ICON_MOUSE: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(6*s), y + (int)(3*s), (int)(12*s), (int)(18*s), (int)(6*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 6), P(12, 10), stroke, color);
        break;
    }
    case ICON_KEYBOARD: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(2*s), y + (int)(5*s), (int)(20*s), (int)(14*s), (int)(2*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(6, 9), P(8, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(11, 9), P(13, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(16, 9), P(18, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(8, 14), P(16, 14), stroke, color);
        break;
    }
    case ICON_FOLDER: {
        draw_vector_line(buf, buf_w, buf_h, P(3, 7), P(9, 7), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(9, 7), P(11, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(11, 9), P(20, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(20, 9), P(20, 19), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(20, 19), P(3, 19), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 19), P(3, 7), stroke, color);
        break;
    }
    case ICON_FILE: {
        draw_vector_line(buf, buf_w, buf_h, P(4, 3), P(14, 3), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(14, 3), P(20, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(20, 9), P(20, 21), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(20, 21), P(4, 21), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(4, 21), P(4, 3), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(14, 3), P(14, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(14, 9), P(20, 9), stroke, color);
        break;
    }
    case ICON_CHEVRON_LEFT: {
        draw_vector_line(buf, buf_w, buf_h, P(15, 5), P(8, 12), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(8, 12), P(15, 19), stroke + 1, color);
        break;
    }
    case ICON_CHEVRON_RIGHT: {
        draw_vector_line(buf, buf_w, buf_h, P(9, 5), P(16, 12), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(16, 12), P(9, 19), stroke + 1, color);
        break;
    }
    case ICON_CHEVRON_UP: {
        draw_vector_line(buf, buf_w, buf_h, P(5, 15), P(12, 8), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 8), P(19, 15), stroke + 1, color);
        break;
    }
    case ICON_CHEVRON_DOWN: {
        draw_vector_line(buf, buf_w, buf_h, P(5, 9), P(12, 16), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 16), P(19, 9), stroke + 1, color);
        break;
    }
    case ICON_VOLUME: {
        draw_vector_line(buf, buf_w, buf_h, P(4, 9), P(9, 9), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(9, 9), P(14, 5), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(14, 5), P(14, 19), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(14, 19), P(9, 15), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(9, 15), P(4, 15), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(4, 15), P(4, 9), stroke, color);
        draw_vector_arc(buf, buf_w, buf_h, P(14, 12), (int)(5*s), -45, 45, stroke, color);
        draw_vector_arc(buf, buf_w, buf_h, P(14, 12), (int)(8*s), -45, 45, stroke, color);
        break;
    }
    case ICON_INFO: {
        draw_vector_circle(buf, buf_w, buf_h, P(12, 12), (int)(9*s), stroke, color);
        draw_vector_disc(buf, buf_w, buf_h, P(12, 8), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 11), P(12, 16), stroke + 1, color);
        break;
    }
    case ICON_GRID: {
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(3*s), y + (int)(3*s), (int)(7*s), (int)(7*s), (int)(1.5f*s), stroke, color);
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(14*s), y + (int)(3*s), (int)(7*s), (int)(7*s), (int)(1.5f*s), stroke, color);
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(3*s), y + (int)(14*s), (int)(7*s), (int)(7*s), (int)(1.5f*s), stroke, color);
        draw_vector_round_rect(buf, buf_w, buf_h, x + (int)(14*s), y + (int)(14*s), (int)(7*s), (int)(7*s), (int)(1.5f*s), stroke, color);
        break;
    }
    case ICON_LAYERS: {
        draw_vector_line(buf, buf_w, buf_h, P(12, 3), P(21, 8), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(21, 8), P(12, 13), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 13), P(3, 8), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 8), P(12, 3), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 12), P(12, 17), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 17), P(21, 12), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 16), P(12, 21), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 21), P(21, 16), stroke, color);
        break;
    }
    case ICON_TERMINAL: {
        draw_vector_line(buf, buf_w, buf_h, P(4, 6), P(10, 12), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(10, 12), P(4, 18), stroke + 1, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 18), P(20, 18), stroke + 1, color);
        break;
    }
    case ICON_GLOBE: {
        draw_vector_circle(buf, buf_w, buf_h, P(12, 12), (int)(9*s), stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(3, 12), P(21, 12), stroke, color);
        draw_vector_arc(buf, buf_w, buf_h, P(12, 12), (int)(9*s), 0, 360, stroke, color);
        break;
    }
    case ICON_POWER: {
        draw_vector_arc(buf, buf_w, buf_h, P(12, 13), (int)(7*s), 45, 315, stroke, color);
        draw_vector_line(buf, buf_w, buf_h, P(12, 4), P(12, 12), stroke + 1, color);
        break;
    }
    default:
        break;
    }
#undef P
}
