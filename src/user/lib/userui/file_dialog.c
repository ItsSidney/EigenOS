/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "file_dialog.h"
#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define DLG_W         640
#define DLG_H         440
#define MAX_EVS       32
#define MAX_ENTRIES   256
#define SIDEBAR_W     140
#define TOPBAR_H      46
#define FOOTER_H      50
#define ROW_H         28

typedef struct {
    char     name[64];
    uint32_t size;
    int      is_dir;
} dlg_entry_t;

static dlg_entry_t dlg_entries[MAX_ENTRIES];
static int         dlg_entry_count = 0;
static dlg_entry_t dlg_filtered[MAX_ENTRIES];
static int         dlg_filtered_count = 0;

static char dlg_cur_path[256] = "/";
static int  dlg_sel_idx = -1;
static int  dlg_scroll_y = 0;

static char dlg_filename[128] = "";
static int  dlg_fn_len = 0;
static int  dlg_fn_focus = 0;

static char dlg_search[64] = "";
static int  dlg_search_len = 0;

static void dlg_apply_filter(const char* filter_ext) {
    dlg_filtered_count = 0;
    for (int i = 0; i < dlg_entry_count; i++) {
        if (dlg_search_len > 0 && strstr(dlg_entries[i].name, dlg_search) == NULL) {
            continue;
        }
        if (!dlg_entries[i].is_dir && filter_ext && filter_ext[0]) {
            int nlen = (int)strlen(dlg_entries[i].name);
            int flen = (int)strlen(filter_ext);
            if (nlen >= flen && strcmp(dlg_entries[i].name + nlen - flen, filter_ext) != 0) {
                continue;
            }
        }
        if (dlg_filtered_count < MAX_ENTRIES) {
            dlg_filtered[dlg_filtered_count++] = dlg_entries[i];
        }
    }
}

static void dlg_scan(const char* path, const char* filter_ext) {
    if (path && path[0]) {
        eigen_fs_chdir(path);
    }
    eigen_fs_pwd(dlg_cur_path, sizeof(dlg_cur_path));

    dlg_entry_count = 0;
    dlg_sel_idx = -1;
    dlg_scroll_y = 0;

    if (strcmp(dlg_cur_path, "/") != 0) {
        dlg_entry_t* e = &dlg_entries[dlg_entry_count++];
        strcpy(e->name, "..");
        e->is_dir = 1;
        e->size = 0;
    }

    char list_buf[4096];
    int len = eigen_fs_list(list_buf, sizeof(list_buf));
    if (len > 0) {
        char* p = list_buf;
        while (*p && dlg_entry_count < MAX_ENTRIES) {
            while (*p == '\n' || *p == '\r') p++;
            if (!*p) break;
            char* start = p;
            while (*p && *p != '\n' && *p != '\r') p++;
            int slen = (int)(p - start);
            if (slen > 0 && slen < 63) {
                char fname[64];
                memcpy(fname, start, slen);
                fname[slen] = 0;

                char clean[64];
                int ci = 0;
                for (int k = 0; k < slen; k++) {
                    if ((unsigned char)fname[k] == 0x1B || fname[k] == '\033') {
                        while (k < slen && fname[k] != 'm') k++;
                        continue;
                    }
                    clean[ci++] = fname[k];
                }
                clean[ci] = 0;
                if (ci == 0 || strcmp(clean, ".") == 0 || strcmp(clean, "./") == 0) continue;

                dlg_entry_t* e = &dlg_entries[dlg_entry_count++];
                strncpy(e->name, clean, 63);
                e->name[63] = 0;

                int is_directory = (clean[ci - 1] == '/');
                if (is_directory) clean[ci - 1] = 0;

                struct eigen_stat st;
                if (eigen_fs_stat(clean, &st) >= 0) {
                    e->is_dir = (st.type == 1) || is_directory;
                    e->size = st.size;
                } else {
                    e->is_dir = is_directory;
                    e->size = (uint32_t)eigen_fs_size(clean);
                }
            }
        }
    }

    if (dlg_entry_count == 0) {
        const char* def_dirs[] = {"user/", "apps/", "wallpapers/", "docs/", NULL};
        for (int i = 0; def_dirs[i] && dlg_entry_count < MAX_ENTRIES; i++) {
            dlg_entry_t* e = &dlg_entries[dlg_entry_count++];
            strncpy(e->name, def_dirs[i], 63);
            e->is_dir = 1;
            e->size = 4096;
        }
    }

    dlg_apply_filter(filter_ext);
}

static void dlg_render(uint32_t* fb, uint32_t w, uint32_t h, int is_save, const char* title_text) {
    if (!fb) return;

    uint32_t bg_main   = 0x0D1117;
    uint32_t top_bg    = 0x161B22;
    uint32_t side_bg   = 0x161B22;
    uint32_t border    = 0x30363D;
    uint32_t text_clr  = 0xE6EDF3;
    uint32_t dim_clr   = 0x8B949E;
    uint32_t accent    = 0x007AFF;
    uint32_t sel_bg    = 0x1F2937;
    uint32_t folder_clr= 0x58A6FF;

    /* 1. Canvas */
    eigen_draw_fillrect(fb, w, h, 0, 0, w, h, bg_main);

    /* 2. Top Bar */
    eigen_draw_fillrect(fb, w, h, 0, 0, w, TOPBAR_H, top_bg);
    eigen_draw_fillrect(fb, w, h, 0, TOPBAR_H - 1, w, 1, border);
    eigen_draw_text(fb, w, h, 14, 14, title_text, text_clr);

    /* Path display */
    int px = SIDEBAR_W + 10;
    int pw = w - px - 14;
    eigen_draw_fillrect(fb, w, h, px, 8, pw, 30, bg_main);
    eigen_draw_rect(fb, w, h, px, 8, pw, 30, border);
    eigen_draw_text(fb, w, h, px + 8, 15, dlg_cur_path, dim_clr);

    /* 3. Left Sidebar (Quick Access) */
    eigen_draw_fillrect(fb, w, h, 0, TOPBAR_H, SIDEBAR_W, h - TOPBAR_H - FOOTER_H, side_bg);
    eigen_draw_fillrect(fb, w, h, SIDEBAR_W - 1, TOPBAR_H, 1, h - TOPBAR_H - FOOTER_H, border);

    eigen_draw_text(fb, w, h, 12, TOPBAR_H + 10, "QUICK ACCESS", dim_clr);
    const char* quick_names[] = { "Root (/)", "User", "Wallpapers", "Docs" };
    for (int i = 0; i < 4; i++) {
        int qy = TOPBAR_H + 34 + i * 28;
        eigen_draw_text(fb, w, h, 20, qy + 6, quick_names[i], text_clr);
    }

    /* 4. File List */
    int lx = SIDEBAR_W + 8;
    int ly = TOPBAR_H + 6;
    int lw = w - lx - 8;
    int lh = h - TOPBAR_H - FOOTER_H - 12;

    int max_rows = lh / ROW_H;
    for (int i = 0; i < max_rows; i++) {
        int idx = i + dlg_scroll_y;
        if (idx >= dlg_filtered_count) break;

        int ry = ly + i * ROW_H;
        if (idx == dlg_sel_idx) {
            eigen_draw_fillrect(fb, w, h, lx, ry, lw, ROW_H, sel_bg);
            eigen_draw_rect(fb, w, h, lx, ry, lw, ROW_H, accent);
        }

        if (dlg_filtered[idx].is_dir) {
            eigen_draw_text(fb, w, h, lx + 8, ry + 6, "[DIR]", folder_clr);
        } else {
            eigen_draw_text(fb, w, h, lx + 8, ry + 6, "[FILE]", dim_clr);
        }

        eigen_draw_text(fb, w, h, lx + 64, ry + 6, dlg_filtered[idx].name, text_clr);

        if (!dlg_filtered[idx].is_dir && dlg_filtered[idx].size > 0) {
            char sz_str[16];
            snprintf(sz_str, sizeof(sz_str), "%u B", dlg_filtered[idx].size);
            eigen_draw_text(fb, w, h, lx + lw - 80, ry + 6, sz_str, dim_clr);
        }
    }

    /* 5. Footer Bar */
    int foot_y = h - FOOTER_H;
    eigen_draw_fillrect(fb, w, h, 0, foot_y, w, FOOTER_H, top_bg);
    eigen_draw_fillrect(fb, w, h, 0, foot_y, w, 1, border);

    if (is_save) {
        eigen_draw_text(fb, w, h, 14, foot_y + 16, "File name:", dim_clr);
        int fn_x = 96;
        int fn_w = w - 280;
        eigen_draw_fillrect(fb, w, h, fn_x, foot_y + 10, fn_w, 30, dlg_fn_focus ? bg_main : 0x21262D);
        eigen_draw_rect(fb, w, h, fn_x, foot_y + 10, fn_w, 30, dlg_fn_focus ? accent : border);
        eigen_draw_text(fb, w, h, fn_x + 8, foot_y + 17, dlg_filename, text_clr);
    } else {
        if (dlg_sel_idx >= 0 && dlg_sel_idx < dlg_filtered_count) {
            char sel_lbl[128];
            snprintf(sel_lbl, sizeof(sel_lbl), "Selected: %s", dlg_filtered[dlg_sel_idx].name);
            eigen_draw_text(fb, w, h, 14, foot_y + 16, sel_lbl, text_clr);
        } else {
            eigen_draw_text(fb, w, h, 14, foot_y + 16, "Select a file to open", dim_clr);
        }
    }

    /* Buttons */
    int btn_w = 80;
    int btn_h = 30;
    int act_x = w - 180;
    int can_x = w - 90;
    int btn_y = foot_y + 10;

    /* Action (Open / Save) */
    eigen_draw_fillrect(fb, w, h, act_x, btn_y, btn_w, btn_h, accent);
    eigen_draw_text(fb, w, h, act_x + (is_save ? 24 : 20), btn_y + 7, is_save ? "Save" : "Open", 0xFFFFFF);

    /* Cancel */
    eigen_draw_fillrect(fb, w, h, can_x, btn_y, btn_w, btn_h, 0x21262D);
    eigen_draw_rect(fb, w, h, can_x, btn_y, btn_w, btn_h, border);
    eigen_draw_text(fb, w, h, can_x + 16, btn_y + 7, "Cancel", text_clr);
}

int eigen_dialog_open(char* out_path, int maxlen, const char* filter_ext) {
    if (!out_path || maxlen <= 0) return 0;

    int win_id = eigen_win_create(140, 100, DLG_W, DLG_H, "Open File");
    if (win_id < 0) return 0;

    uint32_t* win_fb = (uint32_t*)eigen_win_map(win_id);
    uint32_t cur_w = DLG_W, cur_h = DLG_H;
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    dlg_scan("/", filter_ext);

    eigen_ev_t evs[MAX_EVS];
    int result = 0;
    int running = 1;
    uint32_t last_click = 0;
    int last_idx = -1;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        int redraw = 0;

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];

            if (ev->type == EIGEN_EV_CLOSE) {
                running = 0;
                result = 0;
                break;
            }

            if (ev->type == EIGEN_EV_MDOWN) {
                int mx = ev->a, my = ev->b;

                /* Quick Access */
                if (mx < SIDEBAR_W && my >= TOPBAR_H + 34 && my <= TOPBAR_H + 34 + 4 * 28) {
                    int q = (my - (TOPBAR_H + 34)) / 28;
                    if (q == 0) dlg_scan("/", filter_ext);
                    else if (q == 1) dlg_scan("user", filter_ext);
                    else if (q == 2) dlg_scan("wallpapers", filter_ext);
                    else if (q == 3) dlg_scan("docs", filter_ext);
                    redraw = 1;
                }

                /* File List */
                int lx = SIDEBAR_W + 8;
                int ly = TOPBAR_H + 6;
                if (mx >= lx && my >= ly && my < (int)cur_h - FOOTER_H) {
                    int row = (my - ly) / ROW_H + dlg_scroll_y;
                    if (row >= 0 && row < dlg_filtered_count) {
                        uint32_t now = eigen_gettime_ms();
                        if (dlg_sel_idx == row && (now - last_click < 400)) {
                            /* Double click */
                            if (dlg_filtered[row].is_dir) {
                                dlg_scan(dlg_filtered[row].name, filter_ext);
                            } else {
                                snprintf(out_path, maxlen, "%s", dlg_filtered[row].name);
                                result = 1;
                                running = 0;
                            }
                        } else {
                            dlg_sel_idx = row;
                            last_click = now;
                            last_idx = row;
                        }
                        redraw = 1;
                    }
                }

                /* Open button */
                int act_x = (int)cur_w - 180, can_x = (int)cur_w - 90;
                int btn_y = (int)cur_h - FOOTER_H + 10;
                if (my >= btn_y && my <= btn_y + 30) {
                    if (mx >= act_x && mx <= act_x + 80) {
                        if (dlg_sel_idx >= 0 && dlg_sel_idx < dlg_filtered_count) {
                            if (dlg_filtered[dlg_sel_idx].is_dir) {
                                dlg_scan(dlg_filtered[dlg_sel_idx].name, filter_ext);
                                redraw = 1;
                            } else {
                                snprintf(out_path, maxlen, "%s", dlg_filtered[dlg_sel_idx].name);
                                result = 1;
                                running = 0;
                            }
                        }
                    } else if (mx >= can_x && mx <= can_x + 80) {
                        result = 0;
                        running = 0;
                    }
                }
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;

                if (code == 0x48 && dlg_sel_idx > 0) { dlg_sel_idx--; redraw = 1; }
                else if (code == 0x50 && dlg_sel_idx < dlg_filtered_count - 1) { dlg_sel_idx++; redraw = 1; }
                else if (k == '\n' || k == '\r' || code == 0x1C) {
                    if (dlg_sel_idx >= 0 && dlg_sel_idx < dlg_filtered_count) {
                        if (dlg_filtered[dlg_sel_idx].is_dir) {
                            dlg_scan(dlg_filtered[dlg_sel_idx].name, filter_ext);
                            redraw = 1;
                        } else {
                            snprintf(out_path, maxlen, "%s", dlg_filtered[dlg_sel_idx].name);
                            result = 1;
                            running = 0;
                        }
                    }
                }
            }
        }

        dlg_render(win_fb, cur_w, cur_h, 0, "Open File");
        eigen_win_flush(win_id);
        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return result;
}

int eigen_dialog_save(char* out_path, int maxlen, const char* default_name) {
    if (!out_path || maxlen <= 0) return 0;

    int win_id = eigen_win_create(140, 100, DLG_W, DLG_H, "Save File");
    if (win_id < 0) return 0;

    uint32_t* win_fb = (uint32_t*)eigen_win_map(win_id);
    uint32_t cur_w = DLG_W, cur_h = DLG_H;
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    if (default_name && default_name[0]) {
        strncpy(dlg_filename, default_name, 127);
        dlg_fn_len = (int)strlen(dlg_filename);
    } else {
        strcpy(dlg_filename, "untitled.txt");
        dlg_fn_len = (int)strlen(dlg_filename);
    }
    dlg_fn_focus = 1;

    dlg_scan("/", NULL);

    eigen_ev_t evs[MAX_EVS];
    int result = 0;
    int running = 1;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        int redraw = 0;

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];

            if (ev->type == EIGEN_EV_CLOSE) {
                running = 0;
                result = 0;
                break;
            }

            if (ev->type == EIGEN_EV_MDOWN) {
                int mx = ev->a, my = ev->b;

                /* Filename input focus */
                int foot_y = (int)cur_h - FOOTER_H;
                int fn_x = 96, fn_w = (int)cur_w - 280;
                if (my >= foot_y + 10 && my <= foot_y + 40 && mx >= fn_x && mx <= fn_x + fn_w) {
                    dlg_fn_focus = 1;
                    redraw = 1;
                }

                /* Quick Access */
                if (mx < SIDEBAR_W && my >= TOPBAR_H + 34 && my <= TOPBAR_H + 34 + 4 * 28) {
                    int q = (my - (TOPBAR_H + 34)) / 28;
                    if (q == 0) dlg_scan("/", NULL);
                    else if (q == 1) dlg_scan("user", NULL);
                    else if (q == 2) dlg_scan("wallpapers", NULL);
                    else if (q == 3) dlg_scan("docs", NULL);
                    redraw = 1;
                }

                /* File List */
                int lx = SIDEBAR_W + 8;
                int ly = TOPBAR_H + 6;
                if (mx >= lx && my >= ly && my < foot_y) {
                    int row = (my - ly) / ROW_H + dlg_scroll_y;
                    if (row >= 0 && row < dlg_filtered_count) {
                        if (dlg_filtered[row].is_dir) {
                            dlg_scan(dlg_filtered[row].name, NULL);
                        } else {
                            strncpy(dlg_filename, dlg_filtered[row].name, 127);
                            dlg_fn_len = (int)strlen(dlg_filename);
                        }
                        redraw = 1;
                    }
                }

                /* Save / Cancel buttons */
                int act_x = (int)cur_w - 180, can_x = (int)cur_w - 90;
                int btn_y = foot_y + 10;
                if (my >= btn_y && my <= btn_y + 30) {
                    if (mx >= act_x && mx <= act_x + 80) {
                        if (dlg_fn_len > 0) {
                            snprintf(out_path, maxlen, "%s", dlg_filename);
                            result = 1;
                            running = 0;
                        }
                    } else if (mx >= can_x && mx <= can_x + 80) {
                        result = 0;
                        running = 0;
                    }
                }
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;

                if (dlg_fn_focus) {
                    if (k == '\n' || k == '\r' || code == 0x1C) {
                        if (dlg_fn_len > 0) {
                            snprintf(out_path, maxlen, "%s", dlg_filename);
                            result = 1;
                            running = 0;
                        }
                    } else if (k == 8 || code == 0x0E) {
                        if (dlg_fn_len > 0) dlg_filename[--dlg_fn_len] = 0;
                        redraw = 1;
                    } else if (k >= 32 && k < 127 && dlg_fn_len < 120) {
                        dlg_filename[dlg_fn_len++] = k;
                        dlg_filename[dlg_fn_len] = 0;
                        redraw = 1;
                    }
                }
            }
        }

        dlg_render(win_fb, cur_w, cur_h, 1, "Save File");
        eigen_win_flush(win_id);
        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return result;
}
