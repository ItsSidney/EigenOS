/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS — Mandelbrot Explorer (Ring 3) */
#include "userlib.h"
#include "userui.h"
#include "fractal2d.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define WIN_W  800
#define WIN_H  560
#define MAX_EVS 32
#define ITERS  128

static int       win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t  cur_w = WIN_W, cur_h = WIN_H;
static float     cx = -0.5f, cy = 0.0f, scale = 3.5f;
static int       pal = 0, iters = ITERS;
static int       dragging = 0, drag_sx = 0, drag_sy = 0;
static float     drag_cx0, drag_cy0;
static ui_t      g_ui;

static uint32_t render_buf[WIN_W * WIN_H];

static void render(void) {
    int W = (int)cur_w, H = (int)cur_h;
    if (W <= 0 || H <= 0) return;
    f2d_render_mandelbrot(render_buf, W, H, cx, cy, scale, iters, pal);
    /* blit */
    uint32_t* dst = win_fb;
    for (int i = 0; i < W * H; i++) dst[i] = render_buf[i];
}

static void draw_hud(void) {
    uint32_t bar = 0xCC161B22, dim = 0x8B949E, acc = 0x58A6FF;
    int W = (int)cur_w;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, W, 36, 0x161B22);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 35, W, 1, 0x30363D);
    (void)bar;
    char info[128];
    snprintf(info, sizeof(info), "MANDELBROT  cx=%.4f cy=%.4f scale=%.4f iters=%d  pal=%d",
             cx, cy, scale, iters, pal);
    eigen_draw_text(win_fb, cur_w, cur_h, 10, 10, info, dim);
    eigen_draw_text(win_fb, cur_w, cur_h, W - 200, 10,
                    "WASD/Drag:pan  +/-:zoom  P:palette", acc);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(80, 60, WIN_W, WIN_H, "Mandelbrot");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);
    render();
    draw_hud();
    eigen_win_flush(win_id);

    eigen_ev_t evs[MAX_EVS];
    for (;;) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);
        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);
        int dirty = 0;
        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) goto done;
            if (ev->type == EIGEN_EV_MDOWN) {
                dragging = 1;
                drag_sx = ev->a; drag_sy = ev->b;
                drag_cx0 = cx; drag_cy0 = cy;
            }
            if (ev->type == EIGEN_EV_MUP) dragging = 0;
            if (ev->type == EIGEN_EV_MMOVE && dragging) {
                float dx = (float)(drag_sx - ev->a) / (float)cur_w * scale;
                float dy = (float)(drag_sy - ev->b) / (float)cur_h * scale;
                cx = drag_cx0 + dx; cy = drag_cy0 + dy;
                dirty = 1;
            }
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                float step = scale * 0.05f;
                if      (k=='w'||k=='W') { cy -= step; dirty=1; }
                else if (k=='s'||k=='S') { cy += step; dirty=1; }
                else if (k=='a'||k=='A') { cx -= step; dirty=1; }
                else if (k=='d'||k=='D') { cx += step; dirty=1; }
                else if (k=='+'||k=='=') { scale *= 0.8f; dirty=1; }
                else if (k=='-')         { scale *= 1.25f; dirty=1; }
                else if (k=='p'||k=='P') { pal=(pal+1)%4; dirty=1; }
                else if (k=='r'||k=='R') { cx=-0.5f;cy=0.0f;scale=3.5f;dirty=1; }
            }
        }
        if (dirty) { render(); }
        draw_hud();
        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(30);
    }
done:
    eigen_win_close(win_id);
    return 0;
}
