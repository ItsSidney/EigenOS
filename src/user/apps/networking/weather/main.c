/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Modern Weather Forecast (Ring 3)
 *
 * Real-time weather dashboard with temperature, humidity, wind,
 * precipitation, 24-hour forecast, and 5-day outlook.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIN_W        720
#define WIN_H        480
#define MAX_EVS      32

typedef struct {
    char day[8];
    int temp_high;
    int temp_low;
    const char* cond;
    uint32_t color;
} forecast_day_t;

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;

static int current_temp = 22;      /* 22°C */
static int current_humidity = 48;  /* 48% */
static int current_wind = 14;      /* 14 km/h */
static int current_pressure = 1014;/* 1014 hPa */
static const char* condition = "Partly Cloudy";
static char city_name[64] = "New York, USA";

static forecast_day_t week[5] = {
    { "Mon", 24, 16, "Sunny",         0xF59E0B },
    { "Tue", 22, 15, "Partly Cloudy", 0x38BDF8 },
    { "Wed", 19, 13, "Light Rain",    0x60A5FA },
    { "Thu", 21, 14, "Sunny",         0xF59E0B },
    { "Fri", 25, 17, "Clear",         0xF59E0B }
};

static int hourly_temp[8] = { 18, 19, 21, 23, 24, 23, 21, 19 };
static const char* hourly_time[8] = { "09:00", "11:00", "13:00", "15:00", "17:00", "19:00", "21:00", "23:00" };

static void render_all(void) {
    if (!win_fb) return;

    uint32_t bg_main   = 0x0D1117;
    uint32_t card_bg   = 0x161B22;
    uint32_t card_sub  = 0x21262D;
    uint32_t border_clr= 0x30363D;
    uint32_t text_clr  = 0xE6EDF3;
    uint32_t dim_clr   = 0x8B949E;
    uint32_t accent    = 0x38BDF8;

    /* 1. Main Background */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg_main);

    /* 2. Top Header Card (Current Weather) */
    int top_h = 160;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 16, 16, cur_w - 32, top_h, card_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, 16, 16, cur_w - 32, top_h, border_clr);

    /* City & Condition */
    eigen_draw_text(win_fb, cur_w, cur_h, 36, 32, city_name, text_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 36, 56, condition, accent);

    /* Big Temp */
    char t_str[32];
    snprintf(t_str, sizeof(t_str), "%d C", current_temp);
    eigen_draw_text(win_fb, cur_w, cur_h, 36, 88, t_str, text_clr);

    /* Sub stats (Humidity, Wind, Pressure) */
    int stat_x = cur_w - 240;
    char s1[64], s2[64], s3[64];
    snprintf(s1, sizeof(s1), "Humidity:  %d %%", current_humidity);
    snprintf(s2, sizeof(s2), "Wind:      %d km/h", current_wind);
    snprintf(s3, sizeof(s3), "Pressure:  %d hPa", current_pressure);
    eigen_draw_text(win_fb, cur_w, cur_h, stat_x, 40, s1, dim_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, stat_x, 70, s2, dim_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, stat_x, 100, s3, dim_clr);

    /* 3. Hourly Forecast */
    int h_y = 190;
    int h_w = cur_w - 32;
    int h_h = 110;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 16, h_y, h_w, h_h, card_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, 16, h_y, h_w, h_h, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 28, h_y + 10, "TODAY'S HOURLY FORECAST", dim_clr);

    int slot_w = (h_w - 24) / 8;
    for (int i = 0; i < 8; i++) {
        int sx = 28 + i * slot_w;
        eigen_draw_text(win_fb, cur_w, cur_h, sx, h_y + 40, hourly_time[i], dim_clr);
        char ht[16];
        snprintf(ht, sizeof(ht), "%d C", hourly_temp[i]);
        eigen_draw_text(win_fb, cur_w, cur_h, sx + 4, h_y + 70, ht, text_clr);
    }

    /* 4. 5-Day Outlook Cards */
    int d_y = 314;
    int d_w = (cur_w - 32 - 4 * 10) / 5;
    int d_h = cur_h - d_y - 20;

    for (int i = 0; i < 5; i++) {
        int dx = 16 + i * (d_w + 10);
        eigen_draw_fillrect(win_fb, cur_w, cur_h, dx, d_y, d_w, d_h, card_bg);
        eigen_draw_rect(win_fb, cur_w, cur_h, dx, d_y, d_w, d_h, border_clr);

        eigen_draw_text(win_fb, cur_w, cur_h, dx + 16, d_y + 16, week[i].day, text_clr);
        eigen_draw_text(win_fb, cur_w, cur_h, dx + 16, d_y + 44, week[i].cond, week[i].color);

        char range_str[32];
        snprintf(range_str, sizeof(range_str), "%d / %d", week[i].temp_high, week[i].temp_low);
        eigen_draw_text(win_fb, cur_w, cur_h, dx + 16, d_y + 80, range_str, dim_clr);
    }

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    win_id = eigen_win_create(100, 70, WIN_W, WIN_H, "Weather");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        int redraw = 0;

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];

            if (ev->type == EIGEN_EV_CLOSE) {
                running = 0;
                break;
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                if (k == 'r' || k == 'R') {
                    /* Refresh */
                    redraw = 1;
                }
            }
        }

        render_all();
        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return 0;
}
