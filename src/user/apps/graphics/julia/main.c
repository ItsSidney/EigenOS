/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS — Julia Set Explorer (Ring 3) */
#include "userlib.h"
#include "userui.h"
#include "fractal2d.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define WIN_W  800
#define WIN_H  560
#define MAX_EVS 32

static int       win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t  cur_w = WIN_W, cur_h = WIN_H;
static float     cr = -0.7f, ci = 0.27f, scale = 3.0f;
static int       pal = 1;
static ui_t      g_ui;
static uint32_t  render_buf[WIN_W * WIN_H];

static void render(void) {
    f2d_render_julia(render_buf, (int)cur_w, (int)cur_h, cr, ci, scale, 128, pal);
    uint32_t* dst = win_fb;
    for (int i = 0; i < (int)cur_w * (int)cur_h; i++) dst[i] = render_buf[i];
}

static void draw_hud(void) {
    uint32_t dim = 0x8B949E, acc = 0x58A6FF;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, 36, 0x161B22);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 35, cur_w, 1, 0x30363D);
    char info[128];
    snprintf(info, sizeof(info), "JULIA SET  c=%.4f+%.4fi  scale=%.3f  pal=%d",
             cr, ci, scale, pal);
    eigen_draw_text(win_fb, cur_w, cur_h, 10, 10, info, dim);
    eigen_draw_text(win_fb, cur_w, cur_h, (int)cur_w - 220, 10,
                    "WASD:c  +/-:zoom  P:palette  R:reset", acc);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(80, 60, WIN_W, WIN_H, "Julia Set");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);
    render(); draw_hud(); eigen_win_flush(win_id);

    eigen_ev_t evs[MAX_EVS];
    for (;;) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);
        int dirty = 0;
        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) goto done;
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                float step = 0.01f;
                if      (k=='w'||k=='W') { ci -= step; dirty=1; }
                else if (k=='s'||k=='S') { ci += step; dirty=1; }
                else if (k=='a'||k=='A') { cr -= step; dirty=1; }
                else if (k=='d'||k=='D') { cr += step; dirty=1; }
                else if (k=='+'||k=='=') { scale *= 0.8f; dirty=1; }
                else if (k=='-')         { scale *= 1.25f; dirty=1; }
                else if (k=='p'||k=='P') { pal=(pal+1)%4; dirty=1; }
                else if (k=='r'||k=='R') { cr=-0.7f;ci=0.27f;scale=3.0f;dirty=1; }
            }
        }
        if (dirty) render();
        draw_hud();
        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(30);
    }
done:
    eigen_win_close(win_id);
    return 0;
}
