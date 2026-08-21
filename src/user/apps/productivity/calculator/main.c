/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Standard Calculator (Ring 3)
 *
 * Classic "standard" desktop calculator: light gray face, sunken
 * display with black digits, flat beveled square buttons with
 * raised/sunken 3D edges. No rounded corners, no glass, no icons.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define WIN_W 360
#define WIN_H 520
#define MAX_EVS 32
#define UNIT 1000000LL

/* ── Fixed-Point Math Engine ── */
static char    ent[40];
static int     ent_len = 0;
static int64_t acc_m = 0;
static int     op = -1;             /* 0:+, 1:-, 2:*, 3:/ */
static int     entering = 0;
static int     just_eq = 0;
static char    display[40];
static char    expr[64];

static void ent_clear(void) { ent[0] = 0; ent_len = 0; }
static void ent_append(char c) {
    if (ent_len < (int)sizeof(ent) - 2) {
        ent[ent_len++] = c;
        ent[ent_len] = 0;
    }
}

static int64_t parse_micro(const char* s) {
    int neg = 0; const char* p = s;
    if (*p == '-') { neg = 1; p++; }
    int64_t whole = 0, frac = 0, divv = 1;
    int in_frac = 0;
    while ((*p >= '0' && *p <= '9') || *p == '.') {
        if (*p == '.') { in_frac = 1; p++; continue; }
        if (!in_frac) whole = whole * 10 + (*p - '0');
        else { frac = frac * 10 + (*p - '0'); divv *= 10; }
        p++;
    }
    int64_t v = whole * UNIT + (divv > 1 ? (frac * UNIT) / divv : 0);
    return neg ? -v : v;
}

static void fmt_micro(int64_t v, char* out, int n) {
    if (v < 0) { out[0] = '-'; fmt_micro(-v, out + 1, n - 1); return; }
    int64_t whole = v / UNIT;
    int64_t frac = v % UNIT;
    char tmp[32]; int t = 0;
    if (whole == 0) tmp[t++] = '0';
    else {
        int64_t w = whole; char r[24]; int ri = 0;
        while (w) { r[ri++] = (char)('0' + w % 10); w /= 10; }
        while (ri) tmp[t++] = r[--ri];
    }
    if (frac != 0) {
        char fd[8]; int fi = 0; int64_t f = frac;
        for (int i = 0; i < 6; i++) { fd[fi++] = (char)('0' + f * 10 / UNIT); f = (f * 10) % UNIT; }
        while (fi > 0 && fd[fi-1] == '0') fi--;
        if (fi > 0) { tmp[t++] = '.'; for (int i = 0; i < fi; i++) tmp[t++] = fd[i]; }
    }
    tmp[t] = 0;
    int k = 0; while (tmp[k] && k < n - 1) { out[k] = tmp[k]; k++; } out[k] = 0;
}

static void refresh_display(void) {
    if (entering) {
        if (ent_len == 0) { display[0] = '0'; display[1] = 0; }
        else {
            int i = 0;
            while (ent[i] && i < (int)sizeof(display) - 1) { display[i] = ent[i]; i++; }
            display[i] = 0;
        }
    } else {
        fmt_micro(acc_m, display, sizeof(display));
    }
}

static void apply_op(void) {
    int64_t cur = parse_micro(ent_len ? ent : "0");
    if (op < 0) { acc_m = cur; return; }
    switch (op) {
        case 0: acc_m = acc_m + cur; break;
        case 1: acc_m = acc_m - cur; break;
        case 2: acc_m = (acc_m * cur) / UNIT; break;
        case 3: acc_m = (cur == 0) ? 0 : (acc_m * UNIT) / cur; break;
        default: break;
    }
}

static void set_op(int o) {
    if (entering) { apply_op(); ent_clear(); entering = 0; }
    else if (op >= 0 && !just_eq) { apply_op(); }
    op = o; just_eq = 0;
    char a[40]; fmt_micro(acc_m, a, sizeof(a));
    const char* sym = (o==0) ? "+" : (o==1) ? "-" : (o==2) ? "*" : "/";
    int n = 0; while (a[n] && n < (int)sizeof(expr) - 1) { expr[n] = a[n]; n++; }
    expr[n++] = ' ';
    int m = 0; while (sym[m] && n < (int)sizeof(expr) - 1) { expr[n++] = sym[m++]; }
    expr[n] = 0;
    refresh_display();
}

static void do_equal(void) {
    if (entering) { apply_op(); ent_clear(); entering = 0; }
    else if (op >= 0) { apply_op(); }
    op = -1; just_eq = 1; expr[0] = 0;
    fmt_micro(acc_m, display, sizeof(display));
}

static void do_clear(void) {
    acc_m = 0; op = -1; entering = 0; just_eq = 0; ent_clear();
    expr[0] = 0; display[0] = '0'; display[1] = 0;
}

static void do_backspace(void) {
    if (!entering) {
        int i = 0;
        while (display[i] && i < (int)sizeof(ent) - 1) { ent[i] = display[i]; i++; }
        ent[i] = 0; ent_len = i;
        entering = 1; just_eq = 0;
    }
    if (ent_len > 0) { ent[--ent_len] = 0; }
    refresh_display();
}

static void push_digit(int d) {
    if (just_eq) { acc_m = 0; op = -1; expr[0] = 0; just_eq = 0; ent_clear(); }
    if (!entering) { ent_clear(); entering = 1; }
    ent_append((char)('0' + d));
    refresh_display();
}

static void push_dot(void) {
    if (just_eq) { acc_m = 0; op = -1; expr[0] = 0; just_eq = 0; ent_clear(); }
    if (!entering) { ent_clear(); entering = 1; }
    if (ent_len == 0) ent_append('0');
    if (!strchr(ent, '.')) ent_append('.');
    refresh_display();
}

static void do_neg(void) {
    if (entering) {
        if (ent_len > 0 && ent[0] == '-') {
            for (int i = 0; i < ent_len; i++) ent[i] = ent[i+1];
            ent_len--; ent[ent_len] = 0;
        } else if (ent_len < (int)sizeof(ent) - 2) {
            for (int i = ent_len; i >= 0; i--) ent[i+1] = ent[i];
            ent[0] = '-'; ent_len++;
        }
        refresh_display();
    } else { acc_m = -acc_m; refresh_display(); }
}

static void do_pct(void) {
    if (entering) {
        int64_t v = (parse_micro(ent) * UNIT) / 100;
        fmt_micro(v, ent, sizeof(ent));
        ent_len = (int)strlen(ent);
    } else { acc_m = (acc_m * UNIT) / 100; }
    refresh_display();
}

static void on_key(int k) {
    if (k >= '0' && k <= '9') push_digit(k - '0');
    else if (k == '.') push_dot();
    else if (k == '+') set_op(0);
    else if (k == '=') do_equal();
    else if (k == '-') set_op(1);
    else if (k == '*' || k == 'x' || k == 'X') set_op(2);
    else if (k == '/') set_op(3);
    else if (k == '%') do_pct();
    else if (k == 'c' || k == 'C' || k == 27) do_clear();
    else if (k == '\n' || k == '\r') do_equal();
    else if (k == '\b' || k == 127) do_backspace();
}

/* ── Classic palette ── */
static const uint32_t C_FACE     = 0xC9C9CE;   /* calculator body      */
static const uint32_t C_BTN      = 0xDCDCE2;   /* key face             */
static const uint32_t C_BTN_FN   = 0xD2D2D8;   /* function key face    */
static const uint32_t C_LIGHT    = 0xFFFFFF;   /* bevel highlight      */
static const uint32_t C_SHADOW   = 0x8A8A90;   /* bevel shadow         */
static const uint32_t C_DARK     = 0x5A5A60;   /* deep bevel / frame   */
static const uint32_t C_DISP     = 0xFDFDF2;   /* LCD backplate        */
static const uint32_t C_TEXT     = 0x101010;   /* digits / labels      */
static const uint32_t C_EXPR     = 0x60605A;   /* expression line      */

/* 1px beveled edge. Raised: light top/left, dark bottom/right.
   Sunken (pressed / display): dark top/left, light bottom/right. */
static void bevel_rect(uint32_t* fb, int W, int H, int x, int y, int w, int h,
                       int sunken) {
    if (w < 2 || h < 2) return;
    uint32_t hi = sunken ? C_SHADOW : C_LIGHT;
    uint32_t lo = sunken ? C_LIGHT : C_SHADOW;
    eigen_draw_fillrect(fb, W, H, x, y, w, 1, hi);
    eigen_draw_fillrect(fb, W, H, x, y, 1, h, hi);
    eigen_draw_fillrect(fb, W, H, x, y + h - 1, w, 1, lo);
    eigen_draw_fillrect(fb, W, H, x + w - 1, y, 1, h, lo);
    /* hard outer frame */
    eigen_draw_rect(fb, W, H, x, y, w, h, C_DARK);
}

/* Classic flat square key with raised/sunken 3D edges */
static int classic_btn(ui_t* u, int x, int y, int w, int h,
                       const char* label, int is_fn) {
    int hov = u->mx >= x && u->mx < x + w && u->my >= y && u->my < y + h;
    int pressed = hov && u->down;

    eigen_draw_fillrect(u->buf, u->W, u->H, x + 1, y + 1, w - 2, h - 2,
                        pressed ? C_BTN_FN : (is_fn ? C_BTN_FN : C_BTN));
    bevel_rect(u->buf, u->W, u->H, x, y, w, h, pressed);

    int tw = (int)strlen(label) * 8;
    eigen_draw_text(u->buf, u->W, u->H, x + (w - tw) / 2, y + (h - 16) / 2,
                    label, C_TEXT);

    return hov && u->click;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    eigen_printf("[calculator] starting standard ring-3 edition\n");

    int win = eigen_win_create(80, 60, WIN_W, WIN_H, "Calculator");
    if (win < 0) {
        eigen_printf("[calculator] window creation failed\n");
        return 1;
    }

    ui_sync_theme();
    do_clear();

    ui_t ui = {0};

    for (;;) {
        eigen_ev_t evs[MAX_EVS];
        int got = eigen_win_poll(win, evs, MAX_EVS);

        for (int i = 0; i < got; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) goto done;
            if (evs[i].type == EIGEN_EV_KEY) {
                int k = evs[i].a & 0xFF;
                int down = (evs[i].a & 0x100) ? 0 : 1;
                if (down) on_key(k);
            }
        }

        uint32_t W = WIN_W, H = WIN_H;
        eigen_win_getsize(win, &W, &H);
        uint32_t* buf = (uint32_t*)eigen_win_map(win);
        if (!buf) { eigen_sleep_ms(16); continue; }

        ui_begin(&ui, buf, (int)W, (int)H);
        ui_feed(&ui, evs, got);

        /* ── Gray face ── */
        eigen_draw_fillrect(buf, (int)W, (int)H, 0, 0, (int)W, (int)H, C_FACE);

        /* ── Sunken LCD display ── */
        int disp_x = 12, disp_y = 14;
        int disp_w = (int)W - 24, disp_h = 84;
        eigen_draw_fillrect(buf, (int)W, (int)H, disp_x + 1, disp_y + 1,
                            disp_w - 2, disp_h - 2, C_DISP);
        bevel_rect(buf, (int)W, (int)H, disp_x, disp_y, disp_w, disp_h, 1);

        /* Expression line (dim, right-aligned) */
        if (expr[0]) {
            int elen = (int)strlen(expr) * 8;
            int ex_x = disp_x + disp_w - 16 - elen;
            if (ex_x < disp_x + 12) ex_x = disp_x + 12;
            eigen_draw_text(buf, (int)W, (int)H, ex_x, disp_y + 12, expr, C_EXPR);
        }

        /* Main output (right-aligned, black) */
        int dlen = (int)strlen(display) * 8;
        int dx = disp_x + disp_w - 16 - dlen;
        if (dx < disp_x + 12) dx = disp_x + 12;
        eigen_draw_text(buf, (int)W, (int)H, dx, disp_y + 46, display, C_TEXT);

        /* ── Keypad: 4 x 5 flat keys, tight 3px gaps ── */
        int pad_x = 12;
        int pad_y = disp_y + disp_h + 10;
        int pad_w = (int)W - 24;
        int pad_h = (int)H - pad_y - 12;

        int gap = 3;
        int cols = 4;
        int rows = 5;
        int btn_w = (pad_w - gap * (cols - 1)) / cols;
        int btn_h = (pad_h - gap * (rows - 1)) / rows;

        /* Row 0: AC, +/-, %, / */
        int r = 0;
        int y_r = pad_y + r * (btn_h + gap);
        if (classic_btn(&ui, pad_x + 0 * (btn_w + gap), y_r, btn_w, btn_h, "AC", 1)) do_clear();
        if (classic_btn(&ui, pad_x + 1 * (btn_w + gap), y_r, btn_w, btn_h, "+/-", 1)) do_neg();
        if (classic_btn(&ui, pad_x + 2 * (btn_w + gap), y_r, btn_w, btn_h, "%", 1)) do_pct();
        if (classic_btn(&ui, pad_x + 3 * (btn_w + gap), y_r, btn_w, btn_h, "/", 0)) set_op(3);

        /* Row 1: 7, 8, 9, * */
        r = 1;
        y_r = pad_y + r * (btn_h + gap);
        if (classic_btn(&ui, pad_x + 0 * (btn_w + gap), y_r, btn_w, btn_h, "7", 0)) push_digit(7);
        if (classic_btn(&ui, pad_x + 1 * (btn_w + gap), y_r, btn_w, btn_h, "8", 0)) push_digit(8);
        if (classic_btn(&ui, pad_x + 2 * (btn_w + gap), y_r, btn_w, btn_h, "9", 0)) push_digit(9);
        if (classic_btn(&ui, pad_x + 3 * (btn_w + gap), y_r, btn_w, btn_h, "*", 0)) set_op(2);

        /* Row 2: 4, 5, 6, - */
        r = 2;
        y_r = pad_y + r * (btn_h + gap);
        if (classic_btn(&ui, pad_x + 0 * (btn_w + gap), y_r, btn_w, btn_h, "4", 0)) push_digit(4);
        if (classic_btn(&ui, pad_x + 1 * (btn_w + gap), y_r, btn_w, btn_h, "5", 0)) push_digit(5);
        if (classic_btn(&ui, pad_x + 2 * (btn_w + gap), y_r, btn_w, btn_h, "6", 0)) push_digit(6);
        if (classic_btn(&ui, pad_x + 3 * (btn_w + gap), y_r, btn_w, btn_h, "-", 0)) set_op(1);

        /* Row 3: 1, 2, 3, + */
        r = 3;
        y_r = pad_y + r * (btn_h + gap);
        if (classic_btn(&ui, pad_x + 0 * (btn_w + gap), y_r, btn_w, btn_h, "1", 0)) push_digit(1);
        if (classic_btn(&ui, pad_x + 1 * (btn_w + gap), y_r, btn_w, btn_h, "2", 0)) push_digit(2);
        if (classic_btn(&ui, pad_x + 2 * (btn_w + gap), y_r, btn_w, btn_h, "3", 0)) push_digit(3);
        if (classic_btn(&ui, pad_x + 3 * (btn_w + gap), y_r, btn_w, btn_h, "+", 0)) set_op(0);

        /* Row 4: 0, ., Backspace, = */
        r = 4;
        y_r = pad_y + r * (btn_h + gap);
        if (classic_btn(&ui, pad_x + 0 * (btn_w + gap), y_r, btn_w, btn_h, "0", 0)) push_digit(0);
        if (classic_btn(&ui, pad_x + 1 * (btn_w + gap), y_r, btn_w, btn_h, ".", 0)) push_dot();
        if (classic_btn(&ui, pad_x + 2 * (btn_w + gap), y_r, btn_w, btn_h, "BS", 1)) do_backspace();
        if (classic_btn(&ui, pad_x + 3 * (btn_w + gap), y_r, btn_w, btn_h, "=", 0)) do_equal();

        ui_end(&ui);
        eigen_win_flush(win);
        eigen_sleep_ms(16);
    }

done:
    eigen_win_close(win);
    eigen_printf("[calculator] closed\n");
    return 0;
}
