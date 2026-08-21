/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS — Colour Picker / Wheel (Ring 3) */
#include "userlib.h"
#include "userui.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define WIN_W   520
#define WIN_H   440
#define MAX_EVS 32

#define PI 3.14159265358979323846f
#define WHEEL_R  140   /* radius of the hue/sat disk */
#define WHEEL_CX 190   /* centre X */
#define WHEEL_CY 230   /* centre Y */

static int       win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t  cur_w = WIN_W, cur_h = WIN_H;
static int       hue = 210, sat = 80, val = 88; /* 0-360, 0-100, 0-100 */
static int       dragging_wheel = 0, dragging_val = 0;
static ui_t      g_ui;

/* 8 quick-access swatches */
static uint32_t swatches[8] = {
    0xFF6B6B, 0xFF9F43, 0xFECA57, 0x48DBFB,
    0x1DD1A1, 0x5F27CD, 0xC8D6E5, 0x222F3E
};

static uint32_t hsv_to_rgb(int h, int s, int v) {
    if (s <= 0) { uint8_t g=(uint8_t)(v*255/100); return ((uint32_t)g<<16)|((uint32_t)g<<8)|g; }
    h %= 360; if (h < 0) h += 360;
    int reg = h / 60, rem = (h % 60) * 255 / 60;
    int p = v * (100-s) * 255 / 10000;
    int q = v * (6000 - s*rem) * 255 / 600000;
    int t = v * (6000 - s*(60-rem)) * 255 / 600000;
    int vv = v * 255 / 100, r, g, b;
    switch (reg) {
    case 0: r=vv;g=t; b=p;  break; case 1: r=q; g=vv;b=p;  break;
    case 2: r=p; g=vv;b=t;  break; case 3: r=p; g=q; b=vv; break;
    case 4: r=t; g=p; b=vv; break; default:r=vv;g=p; b=q;  break;
    }
#define CL(x) ((x)<0?0:((x)>255?255:(x)))
    return ((uint32_t)CL(r)<<16)|((uint32_t)CL(g)<<8)|(uint32_t)CL(b);
#undef CL
}

static void render_all(void) {
    if (!win_fb) return;
    int W = (int)cur_w, H = (int)cur_h;
    eigen_draw_fillrect(win_fb, W, H, 0, 0, W, H, 0x0D1117);
    /* header */
    eigen_draw_fillrect(win_fb, W, H, 0, 0, W, 36, 0x161B22);
    eigen_draw_fillrect(win_fb, W, H, 0, 35, W, 1, 0x30363D);
    eigen_draw_text(win_fb, W, H, 14, 10, "COLOUR PICKER", 0x8B949E);

    /* hue/sat wheel */
    int r2 = WHEEL_R * WHEEL_R;
    for (int y = WHEEL_CY - WHEEL_R; y <= WHEEL_CY + WHEEL_R; y++) {
        for (int x = WHEEL_CX - WHEEL_R; x <= WHEEL_CX + WHEEL_R; x++) {
            int dx = x - WHEEL_CX, dy = y - WHEEL_CY;
            int d2 = dx*dx + dy*dy;
            if (d2 > r2) continue;
            float r = sqrtf((float)d2);
            int h = (int)(atan2f((float)dy, (float)dx) * 180.0f / PI + 360.0f) % 360;
            int s = (int)(r * 100.0f / WHEEL_R); if (s > 100) s = 100;
            eigen_draw_pixel(win_fb, W, H, x, y, hsv_to_rgb(h, s, val));
        }
    }

    /* cursor on wheel */
    float ang = (float)hue * PI / 180.0f;
    int cx2 = WHEEL_CX + (int)(cosf(ang) * sat * WHEEL_R / 100);
    int cy2 = WHEEL_CY + (int)(sinf(ang) * sat * WHEEL_R / 100);
    eigen_draw_rect(win_fb, W, H, cx2-6, cy2-6, 12, 12, 0xFFFFFF);
    eigen_draw_rect(win_fb, W, H, cx2-5, cy2-5, 10, 10, 0x000000);

    /* value slider (right of wheel) */
    int sx = WHEEL_CX + WHEEL_R + 20, sy = WHEEL_CY - WHEEL_R;
    int sw = 20, sh = WHEEL_R * 2;
    for (int y = sy; y < sy + sh; y++) {
        int v2 = 100 - (y - sy) * 100 / sh;
        uint32_t c = hsv_to_rgb(hue, sat, v2);
        eigen_draw_fillrect(win_fb, W, H, sx, y, sw, 1, c);
    }
    eigen_draw_rect(win_fb, W, H, sx, sy, sw, sh, 0x30363D);
    int vsy = sy + (100 - val) * sh / 100;
    eigen_draw_fillrect(win_fb, W, H, sx - 4, vsy - 2, sw + 8, 4, 0xFFFFFF);

    /* current colour swatch */
    uint32_t picked = hsv_to_rgb(hue, sat, val);
    int px = 360, py = 50;
    eigen_draw_fillrect(win_fb, W, H, px, py, 120, 80, picked);
    eigen_draw_rect(win_fb, W, H, px, py, 120, 80, 0x30363D);

    char hex[12];
    snprintf(hex, sizeof(hex), "#%06X", picked);
    eigen_draw_text(win_fb, W, H, px, py + 88, hex, 0xE6EDF3);

    char rgb_str[32];
    snprintf(rgb_str, sizeof(rgb_str), "R%d G%d B%d",
             (picked>>16)&0xFF, (picked>>8)&0xFF, picked&0xFF);
    eigen_draw_text(win_fb, W, H, px, py + 104, rgb_str, 0x8B949E);

    char hsv_str[32];
    snprintf(hsv_str, sizeof(hsv_str), "H%d S%d V%d", hue, sat, val);
    eigen_draw_text(win_fb, W, H, px, py + 120, hsv_str, 0x8B949E);

    /* swatches */
    eigen_draw_text(win_fb, W, H, 360, 200, "Swatches:", 0x8B949E);
    for (int i = 0; i < 8; i++) {
        int bx = 360 + (i % 4) * 34, by = 218 + (i / 4) * 34;
        eigen_draw_fillrect(win_fb, W, H, bx, by, 30, 30, swatches[i]);
        eigen_draw_rect(win_fb, W, H, bx, by, 30, 30, 0x30363D);
        if (ui_button(&g_ui, bx, by, 30, 30, "")) {
            /* pick swatch */
            uint32_t sc = swatches[i];
            int r=(sc>>16)&0xFF,g=(sc>>8)&0xFF,b=sc&0xFF;
            int mx2=r>g?(r>b?r:b):(g>b?g:b),mn=r<g?(r<b?r:b):(g<b?g:b);
            int d=mx2-mn;
            val=mx2*100/255;
            sat=mx2==0?0:d*100/mx2;
            int hh=0;
            if(d){
                if(mx2==r) hh=(g-b)*60/d;
                else if(mx2==g) hh=120+(b-r)*60/d;
                else hh=240+(r-g)*60/d;
                if(hh<0)hh+=360;
            }
            hue=hh;
        }
    }

    eigen_win_flush(win_id);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(80, 60, WIN_W, WIN_H, "Colour Picker");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    eigen_ev_t evs[MAX_EVS];
    for (;;) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);
        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);
        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) goto done;
            if (ev->type == EIGEN_EV_MDOWN || (ev->type == EIGEN_EV_MMOVE && dragging_wheel)) {
                int mx = ev->a, my = ev->b;
                int dx = mx - WHEEL_CX, dy = my - WHEEL_CY;
                int d2 = dx*dx + dy*dy;
                if (ev->type == EIGEN_EV_MDOWN) {
                    dragging_wheel = (d2 <= WHEEL_R * WHEEL_R);
                    /* val slider? */
                    int sx = WHEEL_CX + WHEEL_R + 20, sy2 = WHEEL_CY - WHEEL_R;
                    if (mx >= sx && mx < sx + 24 && my >= sy2 && my < sy2 + WHEEL_R*2)
                        dragging_val = 1;
                    else dragging_val = 0;
                }
                if (dragging_wheel && d2 <= WHEEL_R * WHEEL_R) {
                    hue = (int)(atan2f((float)dy, (float)dx) * 180.0f / PI + 360.0f) % 360;
                    sat = (int)(sqrtf((float)d2) * 100.0f / WHEEL_R);
                    if (sat > 100) sat = 100;
                }
            }
            if (ev->type == EIGEN_EV_MMOVE && dragging_val) {
                int sy2 = WHEEL_CY - WHEEL_R;
                int rel = ev->b - sy2;
                val = 100 - rel * 100 / (WHEEL_R * 2);
                if (val < 0) val = 0; if (val > 100) val = 100;
            }
            if (ev->type == EIGEN_EV_MUP) { dragging_wheel = 0; dragging_val = 0; }
        }
        render_all();
        ui_end(&g_ui);
        eigen_sleep_ms(30);
    }
done:
    eigen_win_close(win_id);
    return 0;
}
