/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS — Kernel Log Viewer (Ring 3) */
#include "userlib.h"
#include "userui.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define WIN_W   720
#define WIN_H   480
#define MAX_EVS 32
#define MAX_LINES 512
#define LINE_BUF  4096

static int       win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t  cur_w = WIN_W, cur_h = WIN_H;
static char      log_lines[MAX_LINES][80];
static int       line_count = 0;
static int       scroll = 0;
static ui_t      g_ui;

/* Log-level colours */
static uint32_t line_color(const char* s) {
    if (strstr(s, "[ERR]")  || strstr(s, "ERROR") || strstr(s, "FAIL"))  return 0xF85149;
    if (strstr(s, "[WARN]") || strstr(s, "WARN"))                         return 0xE3B341;
    if (strstr(s, "[OK]")   || strstr(s, "SUCCESS") || strstr(s, "[SUCCESS]")) return 0x3FB950;
    if (strstr(s, "[BUILD]")|| strstr(s, "CC ")|| strstr(s, "  LD "))    return 0x58A6FF;
    return 0xC9D1D9;
}

static void load_log(void) {
    static char buf[32768];
    int n = eigen_fs_read_file("bootlog.txt", buf, sizeof(buf) - 1);
    if (n <= 0) {
        /* Try kernel debug output */
        n = eigen_fs_read_file("debug.log", buf, sizeof(buf) - 1);
    }
    if (n <= 0) {
        strcpy(log_lines[0], "[kernellog] No log file found (bootlog.txt / debug.log)");
        line_count = 1;
        return;
    }
    buf[n] = 0;
    line_count = 0;
    char* p = buf;
    while (*p && line_count < MAX_LINES) {
        char* start = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        int len = (int)(p - start);
        if (len > 0) {
            if (len > 78) len = 78;
            memcpy(log_lines[line_count], start, len);
            log_lines[line_count][len] = 0;
            line_count++;
        }
        while (*p == '\n' || *p == '\r') p++;
    }
    scroll = line_count > 20 ? line_count - 20 : 0;
}

static void render_all(void) {
    if (!win_fb) return;
    int W = (int)cur_w, H = (int)cur_h;
    uint32_t bg = 0x0D1117, bar = 0x161B22, border = 0x30363D, dim = 0x8B949E;

    eigen_draw_fillrect(win_fb, W, H, 0, 0, W, H, bg);
    eigen_draw_fillrect(win_fb, W, H, 0, 0, W, 36, bar);
    eigen_draw_fillrect(win_fb, W, H, 0, 35, W, 1, border);
    eigen_draw_text(win_fb, W, H, 14, 10, "KERNEL LOG", dim);

    char cnt_str[40];
    snprintf(cnt_str, sizeof(cnt_str), "%d lines  |  Scroll: Up/Dn  |  R: reload", line_count);
    eigen_draw_text(win_fb, W, H, 130, 10, cnt_str, 0x58A6FF);

    int row_h = 16, start_y = 42;
    int visible = (H - start_y - 26) / row_h;
    for (int i = 0; i < visible; i++) {
        int idx = scroll + i;
        if (idx >= line_count) break;
        int ry = start_y + i * row_h;
        uint32_t col = line_color(log_lines[idx]);
        /* Alternate row shading */
        if (i % 2 == 0) eigen_draw_fillrect(win_fb, W, H, 0, ry, W, row_h, 0x111418);
        eigen_draw_text(win_fb, W, H, 8, ry, log_lines[idx], col);
    }

    /* Scrollbar */
    if (line_count > visible) {
        int sb_h = H - 36 - 26;
        int thumb_h = sb_h * visible / line_count;
        int thumb_y = 36 + scroll * sb_h / line_count;
        eigen_draw_fillrect(win_fb, W, H, W - 8, 36, 8, sb_h, bar);
        eigen_draw_fillrect(win_fb, W, H, W - 8, thumb_y, 8, thumb_h, 0x30363D);
    }

    /* Footer */
    int fy = H - 22;
    eigen_draw_fillrect(win_fb, W, H, 0, fy, W, 22, bar);
    eigen_draw_fillrect(win_fb, W, H, 0, fy, W, 1, border);
    char footer[64];
    snprintf(footer, sizeof(footer), "Line %d/%d", scroll + 1, line_count);
    eigen_draw_text(win_fb, W, H, 10, fy + 4, footer, dim);

    eigen_win_flush(win_id);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(80, 50, WIN_W, WIN_H, "Kernel Log");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);
    load_log();

    eigen_ev_t evs[MAX_EVS];
    for (;;) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);
        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);
        int row_h = 16, visible = ((int)cur_h - 36 - 26) / row_h;
        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) goto done;
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;
                if (code == 0x48) { if (scroll > 0) scroll--; }
                else if (code == 0x50) { if (scroll < line_count - visible) scroll++; }
                else if (code == 0x49) { scroll -= visible; if (scroll < 0) scroll = 0; } /* PgUp */
                else if (code == 0x51) { scroll += visible; if (scroll > line_count - visible) scroll = line_count - visible; } /* PgDn */
                else if (k == 'r' || k == 'R') { load_log(); }
            }
        }
        render_all();
        ui_end(&g_ui);
        eigen_sleep_ms(30);
    }
done:
    eigen_win_close(win_id);
    return 0;
}
