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
#include <stdint.h>

#define WIN_W 680
#define WIN_H 260
#define MAX_EVS 32

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;
static ui_t g_ui;
static int shift_on = 0, caps_on = 0;

typedef struct {
    const char* lbl;
    char base;
    char shifted;
    int wmul; /* 10 = standard 1.0x */
} key_t;

static const key_t row1[] = {
    {"`", '`', '~', 10}, {"1", '1', '!', 10}, {"2", '2', '@', 10}, {"3", '3', '#', 10},
    {"4", '4', '$', 10}, {"5", '5', '%', 10}, {"6", '6', '^', 10}, {"7", '7', '&', 10},
    {"8", '8', '*', 10}, {"9", '9', '(', 10}, {"0", '0', ')', 10}, {"-", '-', '_', 10},
    {"=", '=', '+', 10}, {"Bksp", 8, 8, 15}, {NULL, 0, 0, 0}
};

static const key_t row2[] = {
    {"Tab", '\t', '\t', 14}, {"q", 'q', 'Q', 10}, {"w", 'w', 'W', 10}, {"e", 'e', 'E', 10},
    {"r", 'r', 'R', 10}, {"t", 't', 'T', 10}, {"y", 'y', 'Y', 10}, {"u", 'u', 'U', 10},
    {"i", 'i', 'I', 10}, {"o", 'o', 'O', 10}, {"p", 'p', 'P', 10}, {"[", '[', '{', 10},
    {"]", ']', '}', 10}, {"\\", '\\', '|', 11}, {NULL, 0, 0, 0}
};

static const key_t row3[] = {
    {"Caps", 0, 0, 16}, {"a", 'a', 'A', 10}, {"s", 's', 'S', 10}, {"d", 'd', 'D', 10},
    {"f", 'f', 'F', 10}, {"g", 'g', 'G', 10}, {"h", 'h', 'H', 10}, {"j", 'j', 'J', 10},
    {"k", 'k', 'K', 10}, {"l", 'l', 'L', 10}, {";", ';', ':', 10}, {"'", '\'', '"', 10},
    {"Enter", '\n', '\n', 19}, {NULL, 0, 0, 0}
};

static const key_t row4[] = {
    {"Shift", 0, 0, 20}, {"z", 'z', 'Z', 10}, {"x", 'x', 'X', 10}, {"c", 'c', 'C', 10},
    {"v", 'v', 'V', 10}, {"b", 'b', 'B', 10}, {"n", 'n', 'N', 10}, {"m", 'm', 'M', 10},
    {",", ',', '<', 10}, {".", '.', '>', 10}, {"/", '/', '?', 10}, {"Shift", 0, 0, 25},
    {NULL, 0, 0, 0}
};

static void draw_keyboard(uint32_t* fb, int w, int h) {
    eigen_draw_fillrect(fb, w, h, 0, 0, w, h, 0x161B22);
    eigen_draw_fillrect(fb, w, h, 0, 0, w, 28, 0x0D1117);
    eigen_draw_fillrect(fb, w, h, 0, 27, w, 1, 0x30363D);
    eigen_draw_text(fb, w, h, 12, 6, "ON-SCREEN KEYBOARD (OSK)", 0x58A6FF);

    const key_t* rows[4] = { row1, row2, row3, row4 };
    int start_y = 36;
    int base_w = 40, key_h = 42, gap = 4;

    for (int r = 0; r < 4; r++) {
        int x = 12;
        int y = start_y + r * (key_h + gap);
        for (int k = 0; rows[r][k].lbl; k++) {
            const key_t* key = &rows[r][k];
            int kw = (base_w * key->wmul) / 10;

            int is_active = (strcmp(key->lbl, "Caps") == 0 && caps_on) ||
                            (strcmp(key->lbl, "Shift") == 0 && shift_on);

            if (is_active) {
                eigen_draw_fillrect(fb, w, h, x, y, kw, key_h, 0x1F2937);
                eigen_draw_rect(fb, w, h, x, y, kw, key_h, 0x58A6FF);
            } else {
                eigen_draw_fillrect(fb, w, h, x, y, kw, key_h, 0x21262D);
                eigen_draw_rect(fb, w, h, x, y, kw, key_h, 0x30363D);
            }

            const char* dlbl = (shift_on || caps_on) && key->shifted ? (char[]){key->shifted, 0} : key->lbl;
            eigen_draw_text(fb, w, h, x + (kw - (int)strlen(dlbl) * 8) / 2, y + 14, dlbl, is_active ? 0x58A6FF : 0xE6EDF3);

            if (ui_button(&g_ui, x, y, kw, key_h, "")) {
                if (strcmp(key->lbl, "Caps") == 0) caps_on = !caps_on;
                else if (strcmp(key->lbl, "Shift") == 0) shift_on = !shift_on;
            }

            x += kw + gap;
        }
    }

    /* Space bar row */
    int sp_y = start_y + 4 * (key_h + gap);
    eigen_draw_fillrect(fb, w, h, 140, sp_y, 380, key_h, 0x21262D);
    eigen_draw_rect(fb, w, h, 140, sp_y, 380, key_h, 0x30363D);
    eigen_draw_text(fb, w, h, 310, sp_y + 14, "SPACE", 0x8B949E);
    ui_button(&g_ui, 140, sp_y, 380, key_h, "");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(100, 300, WIN_W, WIN_H, "On-Screen Keyboard");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);

        for (int i = 0; i < n; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) { running = 0; break; }
        }

        draw_keyboard(win_fb, cur_w, cur_h);
        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return 0;
}
