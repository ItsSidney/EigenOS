/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "userlib.h"
#include "userui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

static inline int iabs(int x) { return x < 0 ? -x : x; }

#define WIN_W 760
#define WIN_H 500
#define MAX_EVS 32

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;
static ui_t g_ui;

static float view_cx = 0.0f, view_cy = 0.0f;
static float view_scale = 20.0f; /* pixels per unit */
static int active_preset = 0;

static float eval_fn(int preset, float x) {
    switch (preset) {
    case 0: return sinf(x);
    case 1: return cosf(x);
    case 2: return sinf(x) * 2.0f + cosf(2.0f * x);
    case 3: return 0.2f * x * x - 2.0f;
    case 4: return (x != 0.0f) ? sinf(x * 3.0f) / x : 3.0f;
    case 5: return tanf(x * 0.5f);
    default: return 0.05f * x * x * x - x;
    }
}

static const char* preset_names[] = {
    "f(x) = sin(x)",
    "f(x) = cos(x)",
    "f(x) = 2*sin(x) + cos(2x)",
    "f(x) = 0.2*x^2 - 2",
    "f(x) = sinc(3x)",
    "f(x) = tan(0.5x)",
    "f(x) = 0.05*x^3 - x"
};

static void render_graph(uint32_t* fb, int w, int h) {
    int side_w = 200;
    int gw = w - side_w, gh = h - 40;
    int gx0 = side_w, gy0 = 40;

    /* Background */
    eigen_draw_fillrect(fb, w, h, 0, 0, w, h, 0x0D1117);
    eigen_draw_fillrect(fb, w, h, gx0, gy0, gw, gh, 0x07090E);

    /* Grid lines */
    int screen_cx = gx0 + gw / 2 + (int)(view_cx * view_scale);
    int screen_cy = gy0 + gh / 2 - (int)(view_cy * view_scale);

    /* Vertical grid */
    for (int x = screen_cx % (int)view_scale; x < gx0 + gw; x += (int)view_scale) {
        if (x >= gx0) {
            uint32_t col = (x == screen_cx) ? 0x484F58 : 0x161B22;
            eigen_draw_fillrect(fb, w, h, x, gy0, 1, gh, col);
        }
    }
    /* Horizontal grid */
    for (int y = screen_cy % (int)view_scale; y < gy0 + gh; y += (int)view_scale) {
        if (y >= gy0) {
            uint32_t col = (y == screen_cy) ? 0x484F58 : 0x161B22;
            eigen_draw_fillrect(fb, w, h, gx0, y, gw, 1, col);
        }
    }

    /* Main Axes */
    if (screen_cx >= gx0 && screen_cx < gx0 + gw)
        eigen_draw_fillrect(fb, w, h, screen_cx, gy0, 2, gh, 0x58A6FF);
    if (screen_cy >= gy0 && screen_cy < gy0 + gh)
        eigen_draw_fillrect(fb, w, h, gx0, screen_cy, gw, 2, 0x58A6FF);

    /* Plot Curve */
    uint32_t trace_color = 0x39D353;
    int last_py = -9999;

    for (int px = 0; px < gw; px++) {
        float math_x = (float)(px - (gw / 2 + (int)(view_cx * view_scale))) / view_scale;
        float math_y = eval_fn(active_preset, math_x);
        int py = gy0 + gh / 2 - (int)(math_y * view_scale) - (int)(view_cy * view_scale);

        if (py >= gy0 && py < gy0 + gh) {
            eigen_draw_fillrect(fb, w, h, gx0 + px, py, 2, 2, trace_color);
            if (last_py >= gy0 && last_py < gy0 + gh && iabs(py - last_py) < 40) {
                int ymin = py < last_py ? py : last_py;
                int ymax = py > last_py ? py : last_py;
                eigen_draw_fillrect(fb, w, h, gx0 + px, ymin, 2, ymax - ymin + 1, trace_color);
            }
        }
        last_py = py;
    }

    /* Top Bar */
    eigen_draw_fillrect(fb, w, h, 0, 0, w, 40, 0x161B22);
    eigen_draw_fillrect(fb, w, h, 0, 39, w, 1, 0x30363D);
    eigen_draw_text(fb, w, h, 14, 12, "GRAPHING CALCULATOR", 0x58A6FF);
    eigen_draw_text(fb, w, h, 220, 12, preset_names[active_preset], 0xE6EDF3);
    eigen_draw_text(fb, w, h, w - 240, 12, "Drag: pan | +/-: zoom | R: reset", 0x8B949E);

    /* Left Sidebar */
    eigen_draw_fillrect(fb, w, h, 0, 40, side_w, h - 40, 0x161B22);
    eigen_draw_fillrect(fb, w, h, side_w - 1, 40, 1, h - 40, 0x30363D);

    eigen_draw_text(fb, w, h, 14, 52, "FUNCTION PRESETS", 0x8B949E);
    for (int i = 0; i < 7; i++) {
        int by = 76 + i * 36;
        int sel = (active_preset == i);
        if (sel) {
            eigen_draw_fillrect(fb, w, h, 8, by, side_w - 16, 30, 0x1F2937);
            eigen_draw_rect(fb, w, h, 8, by, side_w - 16, 30, 0x58A6FF);
        } else {
            eigen_draw_fillrect(fb, w, h, 8, by, side_w - 16, 30, 0x21262D);
            eigen_draw_rect(fb, w, h, 8, by, side_w - 16, 30, 0x30363D);
        }
        eigen_draw_text(fb, w, h, 16, by + 8, preset_names[i], sel ? 0x58A6FF : 0xE6EDF3);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(80, 50, WIN_W, WIN_H, "Graphing Calculator");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;
    int dragging = 0, last_mx = 0, last_my = 0;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) { running = 0; break; }
            if (ev->type == EIGEN_EV_MDOWN) {
                int mx = ev->a, my = ev->b;
                if (mx < 200 && my >= 76 && my <= 76 + 7 * 36) {
                    active_preset = (my - 76) / 36;
                    if (active_preset < 0) active_preset = 0;
                    if (active_preset > 6) active_preset = 6;
                } else if (mx >= 200 && my >= 40) {
                    dragging = 1; last_mx = mx; last_my = my;
                }
            }
            if (ev->type == EIGEN_EV_MUP) dragging = 0;
            if (ev->type == EIGEN_EV_MMOVE && dragging) {
                view_cx += (float)(ev->a - last_mx) / view_scale;
                view_cy -= (float)(ev->b - last_my) / view_scale;
                last_mx = ev->a; last_my = ev->b;
            }
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                if (k == '+' || k == '=') view_scale *= 1.25f;
                else if (k == '-') { if (view_scale > 4.0f) view_scale *= 0.8f; }
                else if (k == 'r' || k == 'R') { view_cx = 0; view_cy = 0; view_scale = 20.0f; }
                else if (k >= '1' && k <= '7') active_preset = k - '1';
            }
        }

        render_graph(win_fb, cur_w, cur_h);
        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return 0;
}
