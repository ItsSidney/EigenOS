/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* calendar.c — Calendar app for EigenOS (ring 3, native userui).
 *
 * Simple month grid with a selected-day agenda panel and a quick
 * "add event" modal (title + category color). */

#include "userui.h"
#include "vector_icons.h"

#define WIN_W 680
#define WIN_H 460
#define MAX_EVS 64

/* ── Date math ─────────────────────────────────────────────────────── */
static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}
static int d_in_month(int y, int m) {
    if (m == 2) return is_leap(y) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}
static int dow(int y, int m, int d) {
    int yy = y, mm = m;
    if (mm < 3) { mm += 12; yy--; }
    int k = yy % 100, j = yy / 100;
    int w = (d + (13 * (mm + 1)) / 5 + k + k / 4 + j / 4 - 2 * j + 6) % 7;
    return w < 0 ? w + 7 : w;
}
static int ser(int y, int m, int d) {
    int n = 0;
    for (int yy = 1970; yy < y; yy++) n += is_leap(yy) ? 366 : 365;
    for (int mm = 1; mm < m; mm++) n += d_in_month(y, mm);
    return n + d - 1;
}
static void uns(int s, int* y, int* m, int* d) {
    int yy = 1970;
    for (;;) {
        int len = is_leap(yy) ? 366 : 365;
        if (s < len) break;
        s -= len; yy++;
    }
    for (int mm = 1; mm <= 12; mm++) {
        int len = d_in_month(yy, mm);
        if (s < len) { *y = yy; *m = mm; *d = s + 1; return; }
        s -= len;
    }
    *y = yy; *m = 12; *d = 31;
}
static int month_shift(int s, int off) {
    int y, m, d;
    uns(s, &y, &m, &d);
    int t = y * 12 + (m - 1) + off;
    int ym = t / 12, mm = t % 12 + 1;
    if (d > d_in_month(ym, mm)) d = d_in_month(ym, mm);
    return ser(ym, mm, d);
}

static const char* D_NAMES[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* M_NAMES[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const char* M_SHRT[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ── Events ────────────────────────────────────────────────────────── */
#define MAX_EVTS 256
#define CAT_N 6
static const uint32_t CAT_COLS[CAT_N] = {
    0x3B82F6, 0x10B981, 0xF59E0B, 0xEC4899, 0x8B5CF6, 0x06B6D4
};
typedef struct {
    int day;          /* serial day number */
    int time_min;     /* start time in minutes (0-1439) */
    int color_idx;    /* category color index */
    char title[48];
} cal_evt_t;

static cal_evt_t g_evs[MAX_EVTS];
static int g_ev_count = 0;

static void ev_add(int day, int time_min, int color_idx, const char* title) {
    if (g_ev_count >= MAX_EVTS) return;
    cal_evt_t* e = &g_evs[g_ev_count++];
    e->day = day;
    e->time_min = time_min;
    e->color_idx = color_idx % CAT_N;
    int i = 0;
    while (title && title[i] && i < (int)sizeof(e->title) - 1) { e->title[i] = title[i]; i++; }
    e->title[i] = 0;
}

static void ev_delete(int idx) {
    for (int i = idx; i < g_ev_count - 1; i++) g_evs[i] = g_evs[i + 1];
    g_ev_count--;
}

static void fmt_time(int min, char* buf, int buf_sz) {
    int h = min / 60, m = min % 60;
    int hh = h % 12; if (hh == 0) hh = 12;
    const char* ampm = h < 12 ? "AM" : "PM";
    char tmp[16];
    int n = hh * 100 + m;
    tmp[0] = (char)('0' + n / 1000);
    tmp[1] = (char)('0' + (n / 100) % 10);
    tmp[2] = ':';
    tmp[3] = (char)('0' + (n / 10) % 10);
    tmp[4] = (char)('0' + n % 10);
    tmp[5] = ' ';
    tmp[6] = ampm[0]; tmp[7] = ampm[1];
    tmp[8] = 0;
    int len = 0;
    while (tmp[len] && len < buf_sz - 1) { buf[len] = tmp[len]; len++; }
    buf[len] = 0;
}

/* ── App state ─────────────────────────────────────────────────────── */
static int g_cur_day = 0;      /* selected day (serial) */
static int g_today = 0;        /* real-world today */
static int g_show_modal = 0;
static char g_input_title[48] = "";
static int g_input_color = 0;

/* ── Drawing helpers ───────────────────────────────────────────────── */
static void lbl(uint32_t* fb, int W, int H, int x, int y, const char* s, uint32_t col) {
    eigen_draw_text(fb, W, H, x, y, s, col);
}

static void cal_x(uint32_t* fb, int W, int H, int x, int y, int sz, uint32_t col) {
    for (int i = 0; i < sz; i++) {
        eigen_draw_fillrect(fb, W, H, x + i, y + i, 2, 2, col);
        eigen_draw_fillrect(fb, W, H, x + sz - i, y + i, 2, 2, col);
    }
}

static int in_rect(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* ── Month grid + agenda panel ─────────────────────────────────────── */
static void cal_month_view(ui_t* ui, uint32_t* fb, int W, int H, int mx0, int my0,
                           int mw, int mh) {
    int cyy, cmm, cdd;
    uns(g_cur_day, &cyy, &cmm, &cdd);

    int grid_w = mw - 240;
    int grid_x = mx0 + 10, grid_y = my0 + 4;
    int grid_h = mh - 8;
    int side_x = grid_x + grid_w + 8;
    int side_w = mw - grid_w - 18;
    int cell_w = grid_w / 7;
    int cell_h = (grid_h - 20) / 6;

    for (int c = 0; c < 7; c++)
        lbl(fb, W, H, grid_x + c * cell_w + (cell_w - 24) / 2, grid_y + 2,
            D_NAMES[c], ui_theme.dim);

    int first_s = ser(cyy, cmm, 1) - dow(cyy, cmm, 1);
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 7; c++) {
            int day_s = first_s + r * 7 + c;
            int dx = grid_x + c * cell_w;
            int dy = grid_y + 20 + r * cell_h;
            int d_yy, d_mm, d_dd;
            uns(day_s, &d_yy, &d_mm, &d_dd);

            int is_cur = (d_yy == cyy && d_mm == cmm);
            int is_sel = (day_s == g_cur_day);
            int is_tod = (day_s == g_today);
            int hov = in_rect(ui->mx, ui->my, dx, dy, cell_w - 2, cell_h - 2);

            uint32_t cell_bg = is_sel ? ui_theme.panel2 : (hov ? ui_theme.panel : ui_theme.bg);
            ui_fill_round(fb, W, H, dx, dy, cell_w - 2, cell_h - 2, 6, cell_bg);
            if (is_sel)
                ui_draw_round(fb, W, H, dx, dy, cell_w - 2, cell_h - 2, 6, ui_theme.accent);

            char d_str[4];
            int len = 0;
            int n = d_dd;
            if (n >= 10) d_str[len++] = (char)('0' + n / 10);
            d_str[len++] = (char)('0' + n % 10);
            d_str[len] = 0;
            int tx = dx + (cell_w - 2 - len * 8) / 2;
            int ty = dy + 8;

            if (is_tod) {
                eigen_draw_fillcircle(fb, W, H, dx + (cell_w - 2) / 2, ty + 6, 10,
                                      ui_theme.accent);
                lbl(fb, W, H, tx, ty, d_str, ui_lighten(ui_theme.accent, 80));
            } else {
                lbl(fb, W, H, tx, ty, d_str, is_cur ? (is_sel ? ui_theme.accent : ui_theme.text)
                                                   : ui_theme.dim);
            }

            int has_event = 0;
            for (int e = 0; e < g_ev_count && !has_event; e++) {
                if (g_evs[e].day == day_s) {
                    eigen_draw_fillcircle(fb, W, H, dx + (cell_w - 2) / 2,
                                          dy + cell_h - 10, 3, CAT_COLS[g_evs[e].color_idx]);
                    has_event = 1;
                }
            }
            if (hov && ui->click) g_cur_day = day_s;
        }
    }

    /* right agenda panel */
    int side_h = grid_h;
    ui_fill_round(fb, W, H, side_x, grid_y, side_w, side_h, 8, ui_theme.panel);
    char side_head[48];
    int sl = 0;
    const char* mn = M_SHRT[cmm - 1];
    while (mn[sl] && sl < 10) { side_head[sl] = mn[sl]; sl++; }
    side_head[sl++] = ' ';
    if (cdd >= 10) side_head[sl++] = (char)('0' + cdd / 10);
    side_head[sl++] = (char)('0' + cdd % 10);
    side_head[sl] = 0;
    lbl(fb, W, H, side_x + 14, grid_y + 14, side_head, ui_theme.text);
    lbl(fb, W, H, side_x + 14, grid_y + 32, "SCHEDULED EVENTS", ui_theme.dim);

    int ev_y = grid_y + 54;
    int day_n = 0;
    for (int e = 0; e < g_ev_count; e++) {
        if (g_evs[e].day != g_cur_day) continue;
        if (ev_y + 36 > grid_y + side_h - 46) break;
        ui_fill_round(fb, W, H, side_x + 10, ev_y, side_w - 20, 34, 6, ui_theme.bg);
        eigen_draw_fillrect(fb, W, H, side_x + 10, ev_y + 4, 3, 26, CAT_COLS[g_evs[e].color_idx]);
        char time_str[16];
        fmt_time(g_evs[e].time_min, time_str, sizeof(time_str));
        lbl(fb, W, H, side_x + 20, ev_y + 4, g_evs[e].title, ui_theme.text);
        lbl(fb, W, H, side_x + 20, ev_y + 18, time_str, ui_theme.dim);
        int x0 = side_x + side_w - 36, y0 = ev_y + 9;
        if (in_rect(ui->mx, ui->my, x0, y0, 16, 16)) cal_x(fb, W, H, x0 + 4, y0 + 4, 8, ui_theme.accent);
        else cal_x(fb, W, H, x0 + 4, y0 + 4, 8, ui_theme.dim);
        if (in_rect(ui->mx, ui->my, x0, y0, 16, 16) && ui->click) {
            ev_delete(e);
            break;
        }
        ev_y += 40;
        day_n++;
    }
    if (day_n == 0) {
        lbl(fb, W, H, side_x + 14, grid_y + 60, "No events scheduled", ui_theme.dim);
        lbl(fb, W, H, side_x + 14, grid_y + 78, "Click + to add an event.", ui_theme.dim);
    }

    /* bottom add button */
    int by = grid_y + side_h - 38;
    int hov_a = in_rect(ui->mx, ui->my, side_x + 10, by, side_w - 20, 28);
    ui_fill_round(fb, W, H, side_x + 10, by, side_w - 20, 28, 6,
                  hov_a ? ui_theme.accent : ui_theme.panel2);
    lbl(fb, W, H, side_x + (side_w - 88) / 2, by + 6, "+ Add Event",
        hov_a ? ui_lighten(ui_theme.accent, 80) : ui_theme.text);
    if (hov_a && ui->click) {
        g_show_modal = 1;
        g_input_title[0] = 0;
        ui->focus = 1;
    }
}

/* ── Add-event modal ───────────────────────────────────────────────── */
static void cal_modal(ui_t* ui, uint32_t* fb, int W, int H) {
    int cyy, cmm, cdd;
    uns(g_cur_day, &cyy, &cmm, &cdd);

    for (int i = 0; i < W * H; i++) fb[i] = (fb[i] >> 1) & 0x7F7F7F;

    int mw = 340, mh = 200;
    int mx = (W - mw) / 2, my = (H - mh) / 2 - 20;
    ui_fill_round(fb, W, H, mx, my, mw, mh, 10, ui_theme.panel);
    ui_draw_round(fb, W, H, mx, my, mw, mh, 10, ui_theme.border);
    eigen_draw_fillrect(fb, W, H, mx + 1, my + 1, mw - 2, 2, ui_theme.border2);

    char head[64];
    int hl = 0;
    const char* sm = M_SHRT[cmm - 1];
    while (sm[hl] && hl < 12) { head[hl] = sm[hl]; hl++; }
    head[hl++] = ' ';
    if (cdd >= 10) head[hl++] = (char)('0' + cdd / 10);
    head[hl++] = (char)('0' + cdd % 10);
    head[hl] = 0;
    lbl(fb, W, H, mx + 16, my + 14, head, ui_theme.text);

    int commit = ui_text_input(ui, 1, mx + 16, my + 44, mw - 32, 28, g_input_title, 47);

    lbl(fb, W, H, mx + 16, my + 82, "Category Color", ui_theme.dim);
    int sw_y = my + 100;
    int sw_w = (mw - 32) / CAT_N;
    for (int i = 0; i < CAT_N; i++) {
        int cx = mx + 16 + i * sw_w + sw_w / 2;
        eigen_draw_fillcircle(fb, W, H, cx, sw_y + 12, 7, CAT_COLS[i]);
        if (g_input_color == i)
            eigen_draw_circle(fb, W, H, cx, sw_y + 12, 10, ui_theme.accent);
        if (in_rect(ui->mx, ui->my, cx - 14, sw_y - 2, 28, 28) && ui->click)
            g_input_color = i;
    }

    int cancel = ui_button(ui, mx + 16, my + 156, 110, 28, "Cancel");
    int save = ui_button_col(ui, mx + mw - 126, my + 156, 110, 28, "Save", ui_theme.good);
    if (cancel) {
        g_show_modal = 0;
        ui->focus = -1;
    }
    if (commit || save) {
        if (g_input_title[0])
            ev_add(g_cur_day, 540, g_input_color, g_input_title);
        g_show_modal = 0;
        ui->focus = -1;
    }
}

/* ── Main ──────────────────────────────────────────────────────────── */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    eigen_printf("[calendar] starting (ring 3)\n");

    int win = eigen_win_create(60, 40, WIN_W, WIN_H, "Calendar");
    if (win < 0) { eigen_printf("[calendar] window failed\n"); return 1; }
    ui_sync_theme();

    /* seed defaults from the real clock */
    int t[6];
    eigen_time_get(t);
    g_today = ser(t[5], t[4], t[3]);
    g_cur_day = g_today;
    ev_add(g_today, 570, 0, "Team Standup");
    ev_add(g_today, 840, 1, "Design Review");
    ev_add(g_today + 1, 600, 2, "Project Sync");
    ev_add(g_today + 2, 900, 3, "Doctor Appointment");
    ev_add(g_today + 4, 1140, 4, "Game Night");

    /* Warmup: discard mouse events from the first few frames so the click
       used to launch the calendar does not immediately fire a spurious
       day-selection or modal inside the app. */
    int warmup_frames = 4;

    for (;;) {
        eigen_ev_t evs[MAX_EVS];
        int n = eigen_win_poll(win, evs, MAX_EVS);

        if (warmup_frames > 0) {
            warmup_frames--;
            for (int i = 0; i < n; i++)
                if (evs[i].type == EIGEN_EV_CLOSE) goto done;
            uint32_t fbw2 = 0, fbh2 = 0;
            eigen_win_getsize(win, &fbw2, &fbh2);
            uint32_t* fb2 = (uint32_t*)eigen_win_map(win);
            if (fb2) {
                for (uint32_t pi = 0; pi < fbw2 * fbh2; pi++) fb2[pi] = ui_theme.bg;
                eigen_win_flush(win);
            }
            eigen_sleep_ms(16);
            continue;
        }

        uint32_t fbw = 0, fbh = 0;
        eigen_win_getsize(win, &fbw, &fbh);
        uint32_t* fb = (uint32_t*)eigen_win_map(win);
        if (!fb) { eigen_sleep_ms(16); continue; }

        ui_t ui;
        ui_begin(&ui, fb, (int)fbw, (int)fbh);
        ui_feed(&ui, evs, n);

        int closing = 0;
        for (int i = 0; i < n; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) closing = 1;
            if (evs[i].type == EIGEN_EV_KEY) {
                int k = evs[i].a & 0xFF;
                int down = (evs[i].a & 0x100) ? 0 : 1;
                if (!down) continue;
                if (g_show_modal && k == 27) {
                    g_show_modal = 0;
                    ui.focus = -1;
                }
            }
        }
        if (closing) break;

        for (uint32_t pi = 0; pi < fbw * fbh; pi++) fb[pi] = ui_theme.bg;

        int pw = (int)fbw - 16, ph = (int)fbh - 16;
        if (pw < 64) pw = 64;
        if (ph < 64) ph = 64;

        /* header: < Month Year > [Today] [+ Add] */
        int cyy, cmm, cdd;
        uns(g_cur_day, &cyy, &cmm, &cdd);
        char m_title[32];
        int ml = 0;
        const char* mn = M_NAMES[cmm - 1];
        while (mn[ml] && ml < 20) { m_title[ml] = mn[ml]; ml++; }
        m_title[ml++] = ' ';
        int ylen = 0, yv = cyy;
        char ytmp[8];
        if (yv == 0) ytmp[ylen++] = '0';
        while (yv > 0) { ytmp[ylen++] = (char)('0' + yv % 10); yv /= 10; }
        while (ylen > 0) m_title[ml++] = ytmp[--ylen];
        m_title[ml] = 0;

        int hy = 8, hh = 32;
        if (ui_button_icon(&ui, 8, hy, 32, hh, ICON_CHEVRON_LEFT, NULL, ui_theme.accent))
            g_cur_day = month_shift(g_cur_day, -1);
        if (ui_button_icon(&ui, 44, hy, 32, hh, ICON_CHEVRON_RIGHT, NULL, ui_theme.accent))
            g_cur_day = month_shift(g_cur_day, 1);
        lbl(fb, fbw, fbh, 84, hy + (hh - 16) / 2, m_title, ui_theme.text);
        if (ui_button(&ui, pw - 176, hy, 74, hh, "Today")) g_cur_day = g_today;
        if (ui_button(&ui, pw - 96, hy, 88, hh, "+ Add")) {
            g_show_modal = 1;
            g_input_title[0] = 0;
            ui.focus = 1;
        }

        int canvas_h = ph - hh - 8;
        if (canvas_h < 120) canvas_h = 120;
        if (g_show_modal) cal_modal(&ui, fb, (int)fbw, (int)fbh);
        else              cal_month_view(&ui, fb, (int)fbw, (int)fbh,
                                         8, hy + hh + 8, pw, canvas_h);

        ui_end(&ui);
        eigen_win_flush(win);
        eigen_sleep_ms(16);
    }
done:
    eigen_win_close(win);
    eigen_printf("[calendar] bye\n");
    return 0;
}