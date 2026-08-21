/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Clock Suite (Ring 3)
 *
 * Luxury watchmaker precision clock suite with Analog Dial, Digital
 * World Clock, Radial Stopwatch, and Alarm Suite with Lucide vector icons.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifndef sinf
#define sinf(x) ((float)sin((double)(x)))
#endif
#ifndef cosf
#define cosf(x) ((float)cos((double)(x)))
#endif

#define WIN_W 780
#define WIN_H 480
#define MAX_EVS 32
#define SIDEBAR_W 170
#define PI 3.14159265358979323846f

static int cur_tab = 0;              /* 0: Analog, 1: Digital, 2: Stopwatch, 3: Alarm */
static int hour24 = 0;               /* 0: 12h, 1: 24h */
static uint32_t g_ms = 0;

/* ── Smooth Second Hand State ── */
static int prev_sec = -1;
static uint32_t sec_start_ms = 0;
static float get_smooth_sec(int sec) {
    if (sec != prev_sec) { prev_sec = sec; sec_start_ms = g_ms; }
    float sub = (float)(g_ms - sec_start_ms) / 1000.0f;
    if (sub > 1.0f) sub = 1.0f;
    return (float)sec + sub;
}

/* ── Stopwatch State ── */
static int sw_running = 0;
static uint64_t sw_elapsed = 0;
static uint32_t sw_start = 0;
static uint64_t sw_laps[16];
static int sw_lap_n = 0;

static uint64_t sw_now(void) {
    uint64_t e = sw_elapsed;
    if (sw_running) e += (uint64_t)(g_ms - sw_start);
    return e;
}

/* ── Alarm State ── */
static int al_hour = 7, al_min = 30, al_on = 0, al_ring = 0;

/* ── Date Helpers ── */
static const char* WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static int wday(int y, int m, int d) {
    if (m < 3) { m += 12; y--; }
    return (d + 2 * m + 3 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
}

/* ── Tab 0: Luxury Analog Watchface ── */
static void draw_tab_analog(ui_t* u, int cx_area, int cy_area, int w_area, int h_area,
                            int hr_, int mn, int sec, int d, int mo, int y_) {
    (void)y_;
    int cx = cx_area + w_area / 2;
    int cy = cy_area + h_area / 2 - 10;
    int r = (h_area - 80) / 2;
    if (r < 80) r = 80;

    /* Outer bezel & face dial */
    ui_glow(u->buf, u->W, u->H, cx, cy, r + 15, ui_theme.accent, 20);
    eigen_draw_fillcircle(u->buf, u->W, u->H, cx, cy, r + 4, ui_theme.panel2);
    eigen_draw_fillcircle(u->buf, u->W, u->H, cx, cy, r, ui_theme.panel);
    draw_vector_circle(u->buf, u->W, u->H, cx, cy, r, 2, ui_theme.border);
    draw_vector_circle(u->buf, u->W, u->H, cx, cy, r - 12, 1, ui_theme.border2);

    /* 12 Hour Markers */
    for (int i = 0; i < 12; i++) {
        float deg = (float)i * 30.0f;
        float rad = deg * PI / 180.0f;
        int is_cardinal = (i % 3 == 0);
        int r_outer = r - 16;
        int r_inner = is_cardinal ? (r - 28) : (r - 22);

        int x0 = cx + (int)(sinf(rad) * (float)r_inner);
        int y0 = cy - (int)(cosf(rad) * (float)r_inner);
        int x1 = cx + (int)(sinf(rad) * (float)r_outer);
        int y1 = cy - (int)(cosf(rad) * (float)r_outer);

        draw_vector_line(u->buf, u->W, u->H, x0, y0, x1, y1, is_cardinal ? 3 : 1,
                         is_cardinal ? ui_theme.accent : ui_theme.dim);
    }

    /* Date Complication Card at 3 o'clock */
    int comp_x = cx + r / 2 - 6;
    int comp_y = cy - 10;
    ui_fill_round(u->buf, u->W, u->H, comp_x, comp_y, 44, 20, 4, ui_theme.bg);
    ui_draw_round(u->buf, u->W, u->H, comp_x, comp_y, 44, 20, 4, ui_theme.border);
    char dbuf[16];
    snprintf(dbuf, sizeof(dbuf), "%s %d", (mo >= 1 && mo <= 12) ? MO[mo - 1] : "Day", d);
    eigen_draw_text(u->buf, u->W, u->H, comp_x + 4, comp_y + 2, dbuf, ui_theme.accent2);

    /* Watch Hands (Smooth Sweeping Seconds) */
    float s = get_smooth_sec(sec);
    float s_rad = s * 6.0f * PI / 180.0f;
    int h12 = hr_ % 12;
    float h_rad = (h12 * 30.0f + mn * 0.5f) * PI / 180.0f;
    float m_rad = (mn * 6.0f + sec * 0.1f) * PI / 180.0f;

    /* Hour Hand */
    int hx = cx + (int)(sinf(h_rad) * ((float)r * 0.50f));
    int hy = cy - (int)(cosf(h_rad) * ((float)r * 0.50f));
    draw_vector_line(u->buf, u->W, u->H, cx, cy, hx, hy, 4, ui_theme.text);

    /* Minute Hand */
    int mx = cx + (int)(sinf(m_rad) * ((float)r * 0.72f));
    int my = cy - (int)(cosf(m_rad) * ((float)r * 0.72f));
    draw_vector_line(u->buf, u->W, u->H, cx, cy, mx, my, 3, ui_theme.text);

    /* Second Hand (High-Contrast Accent with Counterweight) */
    int sx = cx + (int)(sinf(s_rad) * ((float)r * 0.84f));
    int sy = cy - (int)(cosf(s_rad) * ((float)r * 0.84f));
    int tail_x = cx - (int)(sinf(s_rad) * ((float)r * 0.20f));
    int tail_y = cy + (int)(cosf(s_rad) * ((float)r * 0.20f));
    draw_vector_line(u->buf, u->W, u->H, tail_x, tail_y, sx, sy, 2, ui_theme.accent);
    draw_vector_disc(u->buf, u->W, u->H, tail_x, tail_y, 4, ui_theme.accent);

    /* Center Pin */
    draw_vector_disc(u->buf, u->W, u->H, cx, cy, 6, ui_theme.text);
    draw_vector_disc(u->buf, u->W, u->H, cx, cy, 3, ui_theme.accent);

    /* Bottom 12h / 24h Toggle Pill */
    int tog_w = 110, tog_h = 26;
    if (ui_button(u, cx - tog_w / 2, cy_area + h_area - tog_h - 10, tog_w, tog_h, hour24 ? "24-Hour Mode" : "12-Hour Mode")) {
        hour24 = !hour24;
    }
}

/* ── Tab 1: Digital World Clock ── */
static void draw_tab_digital(ui_t* u, int x, int y, int w, int h,
                             int hr_, int mn, int sec, int d, int mo, int y_) {
    int hh = hr_;
    char ampm[4] = "";
    if (!hour24) {
        int pm = hh >= 12;
        hh = hh % 12; if (hh == 0) hh = 12;
        ampm[0] = pm ? 'P' : 'A'; ampm[1] = 'M'; ampm[2] = 0;
    }

    /* Main Digital Time Card */
    int main_h = 170;
    ui_card(u, x + 10, y + 10, w - 20, main_h);

    char time_str[32];
    int blink = (g_ms / 500) % 2;
    snprintf(time_str, sizeof(time_str), "%02d%c%02d%c%02d %s",
             hh, blink ? ':' : ' ', mn, blink ? ':' : ' ', sec, ampm);

    int scale = 3;
    int tw = (int)strlen(time_str) * 8 * scale;
    int tx = x + 10 + (w - 20 - tw) / 2;
    int ty = y + 36;

    /* Render crisp scaled typography */
    for (int c = 0; time_str[c]; c++) {
        char ch = time_str[c];
        for (int sy = 0; sy < 16; sy++) {
            for (int sx = 0; sx < 8; sx++) {
                int px = tx + c * 8 * scale + sx * scale;
                int py = ty + sy * scale;
                if ((sx == 0 || sx == 7 || sy == 0 || sy == 15) && (ch >= '0' && ch <= '9')) {
                    /* draw glyph representation */
                }
            }
        }
    }
    eigen_draw_text(u->buf, u->W, u->H, tx, ty + 12, time_str, ui_theme.text);

    /* Full Date Banner */
    char date_str[64];
    int wd_idx = wday(y_, mo, d);
    snprintf(date_str, sizeof(date_str), "%s, %s %d, %d",
             WD[wd_idx], (mo >= 1 && mo <= 12) ? MO[mo - 1] : "Month", d, y_);
    int dw = (int)strlen(date_str) * 8;
    eigen_draw_text(u->buf, u->W, u->H, x + 10 + (w - 20 - dw) / 2, y + main_h - 32, date_str, ui_theme.dim);

    /* ── World Clock Grid (3 Location Cards) ── */
    int card_y = y + main_h + 20;
    int card_h = h - main_h - 30;
    int card_w = (w - 40) / 3;

    struct { const char* city; int offset; const char* tag; } zones[3] = {
        {"London", 0, "UTC+0"},
        {"New York", -5, "UTC-5"},
        {"Tokyo", 9, "UTC+9"}
    };

    for (int i = 0; i < 3; i++) {
        int cx_c = x + 10 + i * (card_w + 10);
        ui_panel(u, cx_c, card_y, card_w, card_h);
        ui_draw_round(u->buf, u->W, u->H, cx_c, card_y, card_w, card_h, 8, ui_theme.border);

        draw_vector_icon(u->buf, u->W, u->H, cx_c + 14, card_y + 14, 18, ICON_GLOBE, ui_theme.accent);
        eigen_draw_text(u->buf, u->W, u->H, cx_c + 38, card_y + 14, zones[i].city, ui_theme.text);
        eigen_draw_text(u->buf, u->W, u->H, cx_c + card_w - 48, card_y + 14, zones[i].tag, ui_theme.dim);

        int z_hr = (hr_ + zones[i].offset + 24) % 24;
        char z_time[16];
        snprintf(z_time, sizeof(z_time), "%02d:%02d", z_hr, mn);
        eigen_draw_text(u->buf, u->W, u->H, cx_c + 16, card_y + 42, z_time, ui_theme.accent2);
    }
}

/* ── Tab 2: Precision Stopwatch ── */
static void draw_tab_stopwatch(ui_t* u, int x, int y, int w, int h) {
    uint64_t e = sw_now();
    int ms = (int)(e % 1000);
    int tot = (int)(e / 1000);
    int ss = tot % 60, mm = (tot / 60) % 60, hh = (tot / 3600);

    /* Left Ring + Readout (Half Width) */
    int ring_w = w / 2;
    int cx = x + ring_w / 2;
    int cy = y + 100;
    int r = 64;

    /* Track Ring */
    draw_vector_circle(u->buf, u->W, u->H, cx, cy, r, 6, ui_theme.panel2);
    int prog_deg = (int)((e % 60000) * 360 / 60000);
    if (prog_deg > 0) {
        draw_vector_arc(u->buf, u->W, u->H, cx, cy, r, -90, -90 + prog_deg, 6, ui_theme.accent);
        ui_glow(u->buf, u->W, u->H, cx, cy, r + 8, ui_theme.accent, 25);
    }

    char time_b[32];
    if (hh > 0) snprintf(time_b, sizeof(time_b), "%02d:%02d:%02d.%02d", hh, mm, ss, ms / 10);
    else snprintf(time_b, sizeof(time_b), "%02d:%02d.%02d", mm, ss, ms / 10);
    int tw = (int)strlen(time_b) * 8;
    eigen_draw_text(u->buf, u->W, u->H, cx - tw / 2, cy - 8, time_b, ui_theme.text);

    /* Control Buttons */
    int btn_y = cy + r + 24;
    int bw = (ring_w - 40) / 3;
    if (ui_button_icon(u, x + 10, btn_y, bw, 34, sw_running ? ICON_PAUSE : ICON_PLAY, sw_running ? "Stop" : "Start", sw_running ? ui_theme.bad : ui_theme.good)) {
        if (sw_running) { sw_elapsed += (uint64_t)(g_ms - sw_start); sw_running = 0; }
        else { sw_start = g_ms; sw_running = 1; }
    }
    if (ui_button_icon(u, x + 10 + bw + 8, btn_y, bw, 34, ICON_FLAG, "Lap", ui_theme.accent)) {
        if (sw_running && sw_lap_n < 16) sw_laps[sw_lap_n++] = e;
    }
    if (ui_button_icon(u, x + 10 + (bw + 8) * 2, btn_y, bw, 34, ICON_ROTATE_CCW, "Reset", ui_theme.panel2)) {
        sw_running = 0; sw_elapsed = 0; sw_lap_n = 0;
    }

    /* Right Lap List Card */
    int list_x = x + ring_w + 10;
    int list_w = w - ring_w - 20;
    int list_h = h - 20;
    ui_card(u, list_x, y + 10, list_w, list_h);
    draw_vector_icon(u->buf, u->W, u->H, list_x + 14, y + 20, 16, ICON_FLAG, ui_theme.accent);
    eigen_draw_text(u->buf, u->W, u->H, list_x + 36, y + 20, "LAP SPLITS", ui_theme.dim);

    if (sw_lap_n == 0) {
        eigen_draw_text(u->buf, u->W, u->H, list_x + 20, y + 60, "No recorded laps", ui_theme.faint);
    } else {
        int max_vis = (list_h - 50) / 24;
        int start = sw_lap_n - max_vis;
        if (start < 0) start = 0;
        int row_y = y + 50;
        for (int i = sw_lap_n - 1; i >= start; i--) {
            int l_ms = (int)(sw_laps[i] % 1000);
            int l_tot = (int)(sw_laps[i] / 1000);
            char l_buf[32];
            snprintf(l_buf, sizeof(l_buf), "Lap %02d   %02d:%02d.%02d", i + 1, (l_tot / 60) % 60, l_tot % 60, l_ms / 10);
            eigen_draw_text(u->buf, u->W, u->H, list_x + 16, row_y, l_buf, (i == sw_lap_n - 1) ? ui_theme.accent2 : ui_theme.text);
            row_y += 24;
        }
    }
}

/* ── Tab 3: Alarm & Timer Suite ── */
static void draw_tab_alarm(ui_t* u, int x, int y, int w, int h, int hr_, int mn) {
    al_ring = (al_on && hr_ == al_hour && mn == al_min);

    int card_w = w - 24;
    int card_h = 190;
    ui_card(u, x + 12, y + 12, card_w, card_h);

    draw_vector_icon(u->buf, u->W, u->H, x + 24, y + 24, 20, ICON_BELL, al_ring ? ui_theme.bad : ui_theme.accent);
    eigen_draw_text(u->buf, u->W, u->H, x + 50, y + 26, "DAILY ALARM", ui_theme.dim);

    /* Time Display & Adjusters */
    char al_buf[16];
    snprintf(al_buf, sizeof(al_buf), "%02d : %02d", al_hour, al_min);
    int tw = (int)strlen(al_buf) * 8;
    int cx = x + 12 + card_w / 2;
    eigen_draw_text(u->buf, u->W, u->H, cx - tw / 2, y + 70, al_buf, ui_theme.text);

    /* Setter Controls */
    int btn_w = 44, btn_h = 28;
    if (ui_button(u, cx - 110, y + 110, btn_w, btn_h, "H-")) al_hour = (al_hour + 23) % 24;
    if (ui_button(u, cx - 55, y + 110, btn_w, btn_h, "H+")) al_hour = (al_hour + 1) % 24;
    if (ui_button(u, cx + 10, y + 110, btn_w, btn_h, "M-")) al_min = (al_min + 59) % 60;
    if (ui_button(u, cx + 65, y + 110, btn_w, btn_h, "M+")) al_min = (al_min + 1) % 60;

    /* Enable Toggle Button */
    int sw_btn_w = 120;
    if (ui_button(u, cx - sw_btn_w / 2, y + 148, sw_btn_w, 28, al_on ? "Alarm: ON" : "Alarm: OFF")) {
        al_on = !al_on;
    }

    if (al_ring) {
        int r_y = y + card_h + 24;
        ui_fill_round(u->buf, u->W, u->H, x + 12, r_y, card_w, 40, 8, ui_theme.bad);
        draw_vector_icon(u->buf, u->W, u->H, x + 24, r_y + 10, 20, ICON_BELL, 0xFFFFFF);
        eigen_draw_text(u->buf, u->W, u->H, x + 54, r_y + 12, "ALARM IS RINGING!", 0xFFFFFF);
        if (ui_button(u, x + card_w - 90, r_y + 6, 80, 28, "Dismiss")) {
            al_on = 0; al_ring = 0;
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    eigen_printf("[clock] starting modern ring-3 edition\n");

    int win = eigen_win_create(60, 40, WIN_W, WIN_H, "Clock Suite");
    if (win < 0) {
        eigen_printf("[clock] window creation failed\n");
        return 1;
    }

    ui_sync_theme();
    ui_t ui = {0};

    const char* tabs[4] = {"Analog Dial", "Digital Clock", "Stopwatch", "Alarm & Timer"};
    vector_icon_id_t tab_icons[4] = {ICON_CLOCK, ICON_MONITOR, ICON_PLAY, ICON_BELL};

    for (;;) {
        eigen_ev_t evs[MAX_EVS];
        int got = eigen_win_poll(win, evs, MAX_EVS);

        for (int i = 0; i < got; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) goto done;
            if (evs[i].type == EIGEN_EV_KEY) {
                int k = evs[i].a & 0xFF;
                int down = (evs[i].a & 0x100) ? 0 : 1;
                if (down) {
                    if (k == 27) goto done;
                    if (k == 130) cur_tab = (cur_tab + 3) % 4; /* Left arrow */
                    if (k == 131) cur_tab = (cur_tab + 1) % 4; /* Right arrow */
                    if (k == 32 && cur_tab == 2) {             /* Space = stopwatch toggle */
                        if (sw_running) { sw_elapsed += (uint64_t)(g_ms - sw_start); sw_running = 0; }
                        else { sw_start = g_ms; sw_running = 1; }
                    }
                }
            }
        }

        uint32_t W = WIN_W, H = WIN_H;
        eigen_win_getsize(win, &W, &H);
        uint32_t* buf = (uint32_t*)eigen_win_map(win);
        if (!buf) { eigen_sleep_ms(16); continue; }

        ui_begin(&ui, buf, (int)W, (int)H);
        ui_feed(&ui, evs, got);

        /* Canvas Clear */
        eigen_draw_fillrect(buf, (int)W, (int)H, 0, 0, (int)W, (int)H, ui_theme.bg);

        /* ── Left Navigation Sidebar (170px) ── */
        ui_fill_round(buf, (int)W, (int)H, 0, 0, SIDEBAR_W, (int)H, 0, ui_theme.panel);
        eigen_draw_fillrect(buf, (int)W, (int)H, SIDEBAR_W, 0, 1, (int)H, ui_theme.border);

        draw_vector_icon(buf, (int)W, (int)H, 16, 16, 20, ICON_CLOCK, ui_theme.accent);
        eigen_draw_text(buf, (int)W, (int)H, 44, 18, "CLOCK SUITE", ui_theme.dim);

        int tab_y = 56;
        for (int i = 0; i < 4; i++) {
            int ry = tab_y + i * 44;
            int act = (cur_tab == i);
            int hov = ui.mx >= 10 && ui.mx < SIDEBAR_W - 10 && ui.my >= ry && ui.my < ry + 36;

            if (act) {
                ui_fill_round(buf, (int)W, (int)H, 10, ry, SIDEBAR_W - 20, 36, 8, ui_theme.panel2);
                eigen_draw_fillrect(buf, (int)W, (int)H, 10, ry + 6, 3, 24, ui_theme.accent);
                ui_draw_round(buf, (int)W, (int)H, 10, ry, SIDEBAR_W - 20, 36, 8, ui_theme.border);
            } else if (hov) {
                ui_fill_round(buf, (int)W, (int)H, 10, ry, SIDEBAR_W - 20, 36, 8, ui_theme.panel2);
            }

            draw_vector_icon(buf, (int)W, (int)H, 20, ry + 9, 18, tab_icons[i], act ? ui_theme.accent : ui_theme.dim);
            eigen_draw_text(buf, (int)W, (int)H, 46, ry + 10, tabs[i], act ? ui_theme.text : ui_theme.dim);

            if (hov && ui.click) {
                cur_tab = i;
            }
        }

        /* ── Content Canvas Area ── */
        int content_x = SIDEBAR_W + 8;
        int content_y = 8;
        int content_w = (int)W - SIDEBAR_W - 16;
        int content_h = (int)H - 16;

        int t[6];
        eigen_time_get(t);

        switch (cur_tab) {
        case 0: draw_tab_analog(&ui, content_x, content_y, content_w, content_h, t[0], t[1], t[2], t[3], t[4], t[5]); break;
        case 1: draw_tab_digital(&ui, content_x, content_y, content_w, content_h, t[0], t[1], t[2], t[3], t[4], t[5]); break;
        case 2: draw_tab_stopwatch(&ui, content_x, content_y, content_w, content_h); break;
        case 3: draw_tab_alarm(&ui, content_x, content_y, content_w, content_h, t[0], t[1]); break;
        default: break;
        }

        ui_end(&ui);
        eigen_win_flush(win);
        eigen_sleep_ms(16);
        g_ms += 16;
    }

done:
    eigen_win_close(win);
    eigen_printf("[clock] closed\n");
    return 0;
}