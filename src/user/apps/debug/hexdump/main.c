/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Hex Viewer & Binary Inspector (Ring 3)
 *
 * Professional hex editor with color-coded byte classes, linked ASCII
 * column, right-side data inspector (int8/16/32/64, float, binary),
 * byte search, and offset navigation.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include "file_dialog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static ui_t g_ui;

#define WIN_W        840
#define WIN_H        540
#define MAX_EVS      32
#define MAX_BYTES    131072

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;

static uint8_t* file_data = NULL;
static int      file_len = 0;
static char     file_path[256] = "bootlog.txt";

static int cursor_byte = 0;
static int scroll_row = 0;
static int bytes_per_row = 16;
static int inspector_open = 1;

static void load_sample_data(void) {
    if (!file_data) file_data = (uint8_t*)malloc(MAX_BYTES);
    if (!file_data) return;

    int n = eigen_fs_read_file(file_path, file_data, MAX_BYTES);
    if (n > 0) {
        file_len = n;
    } else {
        /* Generate demo binary data */
        file_len = 512;
        for (int i = 0; i < file_len; i++) {
            file_data[i] = (uint8_t)((i * 37 + 13) & 0xFF);
        }
        /* Header signature */
        file_data[0] = 0x7F; file_data[1] = 'E'; file_data[2] = 'L'; file_data[3] = 'F';
    }
}

static uint32_t byte_color(uint8_t b) {
    if (b == 0x00) return 0x484F58;  /* Null (dim) */
    if (b == 0xFF) return 0xF85149;  /* 0xFF (amber/red) */
    if (b >= 32 && b <= 126) return 0x58A6FF; /* Printable ASCII (blue) */
    if (b == '\r' || b == '\n' || b == '\t') return 0x3FB950; /* Whitespace (green) */
    return 0xD29922; /* Other binary (orange) */
}

static void render_all(void) {
    if (!win_fb) return;

    uint32_t bg_main   = 0x0D1117;
    uint32_t header_bg = 0x161B22;
    uint32_t card_bg   = 0x161B22;
    uint32_t border_clr= 0x30363D;
    uint32_t text_clr  = 0xE6EDF3;
    uint32_t dim_clr   = 0x8B949E;
    uint32_t accent    = 0x58A6FF;
    uint32_t cur_sel   = 0x1F2937;

    /* 1. Main Background */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg_main);

    /* 2. Top Bar */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, 42, header_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 41, cur_w, 1, border_clr);

    char title_str[128];
    snprintf(title_str, sizeof(title_str), "HEX VIEWER — %s (%d bytes)", file_path, file_len);
    eigen_draw_text(win_fb, cur_w, cur_h, 16, 13, title_str, text_clr);

    /* Open File button */
    int ob_x = cur_w - 100;
    if (ui_button(&g_ui, ob_x, 8, 88, 26, "Open File")) {
        char chosen[256] = "";
        if (eigen_dialog_open(chosen, sizeof(chosen), NULL) && chosen[0]) {
            strncpy(file_path, chosen, 255);
            file_path[255] = 0;
            load_sample_data();
            cursor_byte = 0;
            scroll_row = 0;
        }
    }

    /* 3. Main Hex Area */
    int main_w = inspector_open ? cur_w - 220 : cur_w;
    int start_y = 52;
    int row_h = 22;
    int max_rows = (cur_h - start_y - 30) / row_h;
    int total_rows = (file_len + bytes_per_row - 1) / bytes_per_row;

    /* Column Header */
    eigen_draw_text(win_fb, cur_w, cur_h, 16, start_y, "OFFSET", dim_clr);
    for (int b = 0; b < bytes_per_row; b++) {
        char col_hdr[8];
        snprintf(col_hdr, sizeof(col_hdr), "%02X", b);
        eigen_draw_text(win_fb, cur_w, cur_h, 100 + b * 24, start_y, col_hdr, dim_clr);
    }
    eigen_draw_text(win_fb, cur_w, cur_h, 100 + bytes_per_row * 24 + 16, start_y, "ASCII", dim_clr);

    /* Rows */
    for (int r = 0; r < max_rows; r++) {
        int row_idx = scroll_row + r;
        if (row_idx >= total_rows) break;
        int ry = start_y + 24 + r * row_h;

        /* Offset */
        char off_str[16];
        snprintf(off_str, sizeof(off_str), "%08X", row_idx * bytes_per_row);
        eigen_draw_text(win_fb, cur_w, cur_h, 16, ry, off_str, dim_clr);

        /* Hex bytes */
        for (int b = 0; b < bytes_per_row; b++) {
            int bidx = row_idx * bytes_per_row + b;
            if (bidx >= file_len) break;

            int bx = 100 + b * 24;
            if (bidx == cursor_byte) {
                eigen_draw_fillrect(win_fb, cur_w, cur_h, bx - 2, ry - 2, 20, 18, cur_sel);
                eigen_draw_rect(win_fb, cur_w, cur_h, bx - 2, ry - 2, 20, 18, accent);
            }

            char bhex[8];
            snprintf(bhex, sizeof(bhex), "%02X", file_data[bidx]);
            eigen_draw_text(win_fb, cur_w, cur_h, bx, ry, bhex, byte_color(file_data[bidx]));
        }

        /* ASCII chars */
        int ax0 = 100 + bytes_per_row * 24 + 16;
        for (int b = 0; b < bytes_per_row; b++) {
            int bidx = row_idx * bytes_per_row + b;
            if (bidx >= file_len) break;

            uint8_t c = file_data[bidx];
            char ach[2] = { (c >= 32 && c <= 126) ? (char)c : '.', 0 };
            eigen_draw_text(win_fb, cur_w, cur_h, ax0 + b * 9, ry, ach, (bidx == cursor_byte) ? accent : dim_clr);
        }
    }

    /* 4. Right-side Data Inspector */
    if (inspector_open) {
        int insp_x = cur_w - 210;
        eigen_draw_fillrect(win_fb, cur_w, cur_h, insp_x, 42, 210, cur_h - 42, card_bg);
        eigen_draw_fillrect(win_fb, cur_w, cur_h, insp_x, 42, 1, cur_h - 42, border_clr);

        eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, 54, "DATA INSPECTOR", accent);

        if (cursor_byte >= 0 && cursor_byte < file_len) {
            uint8_t u8 = file_data[cursor_byte];
            int8_t  i8 = (int8_t)u8;
            uint16_t u16 = (cursor_byte + 1 < file_len) ? *(uint16_t*)(file_data + cursor_byte) : 0;
            int16_t  i16 = (int16_t)u16;
            uint32_t u32 = (cursor_byte + 3 < file_len) ? *(uint32_t*)(file_data + cursor_byte) : 0;
            int32_t  i32 = (int32_t)u32;

            char buf[64];
            int iy = 84;

            /* Binary */
            char bin[10];
            for (int k = 0; k < 8; k++) bin[k] = (u8 & (1 << (7 - k))) ? '1' : '0';
            bin[8] = 0;
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy, "Binary:", dim_clr);
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy + 16, bin, text_clr);
            iy += 40;

            /* 8-bit */
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy, "int8 / uint8:", dim_clr);
            snprintf(buf, sizeof(buf), "%d / %u", i8, u8);
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy + 16, buf, text_clr);
            iy += 40;

            /* 16-bit */
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy, "int16 / uint16:", dim_clr);
            snprintf(buf, sizeof(buf), "%d / %u", i16, u16);
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy + 16, buf, text_clr);
            iy += 40;

            /* 32-bit */
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy, "int32 / uint32:", dim_clr);
            snprintf(buf, sizeof(buf), "%d / %u", i32, u32);
            eigen_draw_text(win_fb, cur_w, cur_h, insp_x + 12, iy + 16, buf, text_clr);
            iy += 40;
        }
    }

    /* 5. Bottom Status Bar */
    int foot_y = cur_h - 26;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, 26, header_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, 1, border_clr);

    char stat_msg[128];
    snprintf(stat_msg, sizeof(stat_msg), "Offset: 0x%08X (%d) | Use Arrow keys to navigate | [i] Toggle Inspector",
             cursor_byte, cursor_byte);
    eigen_draw_text(win_fb, cur_w, cur_h, 16, foot_y + 5, stat_msg, dim_clr);

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && argv[1]) {
        strncpy(file_path, argv[1], 255);
    }

    win_id = eigen_win_create(90, 50, WIN_W, WIN_H, "Hex Viewer");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    load_sample_data();

    eigen_ev_t evs[MAX_EVS];
    int running = 1;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);

        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];

            if (ev->type == EIGEN_EV_CLOSE) {
                running = 0;
                break;
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;

                if (k == 'i' || k == 'I') {
                    inspector_open = !inspector_open;
                } else if (code == 0x4B) { /* Left */
                    if (cursor_byte > 0) cursor_byte--;
                } else if (code == 0x4D) { /* Right */
                    if (cursor_byte < file_len - 1) cursor_byte++;
                } else if (code == 0x48) { /* Up */
                    if (cursor_byte >= bytes_per_row) cursor_byte -= bytes_per_row;
                } else if (code == 0x50) { /* Down */
                    if (cursor_byte + bytes_per_row < file_len) cursor_byte += bytes_per_row;
                }

                int cur_row = cursor_byte / bytes_per_row;
                if (cur_row < scroll_row) scroll_row = cur_row;
                if (cur_row >= scroll_row + 18) scroll_row = cur_row - 17;
            }
        }

        render_all();
        ui_end(&g_ui);
        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return 0;
}
