/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Modern File Explorer (Ring 3)
 *
 * Rich desktop file manager with sidebar quick access, breadcrumb navigation,
 * list/grid view, vector icons, instant search, file properties, and
 * ELF app launching.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIN_W          780
#define WIN_H          520
#define MAX_EVS        32
#define MAX_FILES      256
#define SIDEBAR_W      170
#define TOPBAR_H       50
#define STATUSBAR_H    26
#define ROW_H          36
#define GRID_COLS      5
#define CARD_W         96
#define CARD_H         94

typedef struct {
    char     name[64];
    uint32_t size;
    int      is_dir;
} fe_entry_t;

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;

static fe_entry_t entries[MAX_FILES];
static int        entry_count = 0;
static fe_entry_t filtered[MAX_FILES];
static int        filtered_count = 0;

static char cur_path[256] = "/";
static int  selected_idx = -1;
static int  scroll_y = 0;
static int  view_mode = 0; /* 0 = List, 1 = Grid */

static char search_txt[64] = "";
static int  search_len = 0;
static int  search_focus = 0;

static char history[32][256];
static int  hist_count = 0;
static int  hist_idx = -1;

static uint32_t last_click_time = 0;
static int      last_click_idx = -1;

/* Forward declarations */
static void scan_directory(const char* path);
static void render_all(void);

static void push_history(const char* path) {
    if (hist_count == 0 || strcmp(history[hist_count - 1], path) != 0) {
        if (hist_count < 32) {
            strncpy(history[hist_count], path, 255);
            hist_idx = hist_count++;
        }
    }
}

static void apply_search_filter(void) {
    filtered_count = 0;
    for (int i = 0; i < entry_count; i++) {
        if (search_len == 0 || strstr(entries[i].name, search_txt) != NULL) {
            if (filtered_count < MAX_FILES) {
                filtered[filtered_count++] = entries[i];
            }
        }
    }
}

static void scan_directory(const char* path) {
    if (path && path[0]) {
        eigen_fs_chdir(path);
    }
    eigen_fs_pwd(cur_path, sizeof(cur_path));
    push_history(cur_path);

    entry_count = 0;
    selected_idx = -1;
    scroll_y = 0;

    /* If not at root, add parent directory entry ".." */
    if (strcmp(cur_path, "/") != 0) {
        fe_entry_t* e = &entries[entry_count++];
        strcpy(e->name, "..");
        e->is_dir = 1;
        e->size = 0;
    }

    /* Get directory list from kernel */
    char list_buf[4096];
    int len = eigen_fs_list(list_buf, sizeof(list_buf));
    if (len > 0) {
        char* p = list_buf;
        while (*p && entry_count < MAX_FILES) {
            while (*p == '\n' || *p == '\r') p++;
            if (!*p) break;
            char* start = p;
            while (*p && *p != '\n' && *p != '\r') p++;
            int slen = (int)(p - start);
            if (slen > 0 && slen < 63) {
                char fname[64];
                memcpy(fname, start, slen);
                fname[slen] = 0;

                /* Strip ANSI color codes */
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

                fe_entry_t* e = &entries[entry_count++];
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

    if (entry_count == 0) {
        /* Standard initial root directories if filesystem root is empty */
        const char* def_dirs[] = {"user/", "apps/", "system/", "wallpapers/", "docs/", NULL};
        for (int i = 0; def_dirs[i] && entry_count < MAX_FILES; i++) {
            fe_entry_t* e = &entries[entry_count++];
            strncpy(e->name, def_dirs[i], 63);
            e->is_dir = 1;
            e->size = 4096;
        }
        const char* def_files[] = {"bootlog.txt", "readme.txt", NULL};
        for (int i = 0; def_files[i] && entry_count < MAX_FILES; i++) {
            fe_entry_t* e = &entries[entry_count++];
            strncpy(e->name, def_files[i], 63);
            e->is_dir = 0;
            e->size = 1024;
        }
    }

    apply_search_filter();
}

static void format_size(uint32_t bytes, char* out, int maxlen) {
    if (bytes < 1024) {
        snprintf(out, maxlen, "%u B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(out, maxlen, "%u KB", bytes / 1024);
    } else {
        snprintf(out, maxlen, "%u MB", bytes / (1024 * 1024));
    }
}

static void render_all(void) {
    if (!win_fb) return;

    uint32_t bg_main   = 0x0D1117;
    uint32_t sidebar_bg= 0x161B22;
    uint32_t header_bg = 0x161B22;
    uint32_t border_clr= 0x30363D;
    uint32_t text_clr  = 0xE6EDF3;
    uint32_t dim_clr   = 0x8B949E;
    uint32_t accent    = 0x007AFF;
    uint32_t folder_clr= 0x58A6FF;
    uint32_t sel_bg    = 0x1F2937;

    /* 1. Main Background */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg_main);

    /* 2. Top Bar */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, TOPBAR_H, header_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, TOPBAR_H - 1, cur_w, 1, border_clr);

    /* Nav Buttons */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 12, 10, 30, 30, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, 12, 10, 30, 30, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 20, 17, "<-", text_clr);

    eigen_draw_fillrect(win_fb, cur_w, cur_h, 48, 10, 30, 30, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, 48, 10, 30, 30, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 56, 17, "->", text_clr);

    /* Breadcrumb Path Bar */
    int path_x = 88;
    int path_w = cur_w - 360;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, path_x, 10, path_w, 30, 0x0D1117);
    eigen_draw_rect(win_fb, cur_w, cur_h, path_x, 10, path_w, 30, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, path_x + 10, 17, cur_path, text_clr);

    /* View Switcher */
    int view_btn_x = cur_w - 260;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, view_btn_x, 10, 60, 30, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, view_btn_x, 10, 60, 30, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, view_btn_x + 8, 17, view_mode == 0 ? "Grid" : "List", text_clr);

    /* Search Box */
    int s_x = cur_w - 190;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, s_x, 10, 180, 30, search_focus ? 0x0D1117 : 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, s_x, 10, 180, 30, search_focus ? accent : border_clr);
    if (search_len > 0) {
        char sdisp[64];
        snprintf(sdisp, sizeof(sdisp), "%s%s", search_txt, (search_focus && (eigen_gettime_ms()/500)%2) ? "_" : "");
        eigen_draw_text(win_fb, cur_w, cur_h, s_x + 8, 17, sdisp, text_clr);
    } else {
        eigen_draw_text(win_fb, cur_w, cur_h, s_x + 8, 17, "Search files...", dim_clr);
    }

    /* 3. Sidebar */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, TOPBAR_H, SIDEBAR_W, cur_h - TOPBAR_H - STATUSBAR_H, sidebar_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, SIDEBAR_W - 1, TOPBAR_H, 1, cur_h - TOPBAR_H - STATUSBAR_H, border_clr);

    eigen_draw_text(win_fb, cur_w, cur_h, 16, TOPBAR_H + 12, "QUICK ACCESS", dim_clr);

    const char* quick_names[] = { "Root (/)", "User Apps", "Wallpapers", "Drivers", "Vendors" };
    for (int i = 0; i < 5; i++) {
        int sy = TOPBAR_H + 36 + i * 30;
        eigen_draw_text(win_fb, cur_w, cur_h, 24, sy + 6, quick_names[i], text_clr);
    }

    /* 4. Content Area */
    int cont_x = SIDEBAR_W + 12;
    int cont_y = TOPBAR_H + 8;
    int cont_w = cur_w - cont_x - 12;
    int cont_h = cur_h - cont_y - STATUSBAR_H - 8;

    if (view_mode == 0) {
        /* List View */
        int cy = cont_y - scroll_y;
        for (int i = 0; i < filtered_count; i++) {
            if (cy + ROW_H >= cont_y && cy < cont_y + cont_h) {
                if (i == selected_idx) {
                    eigen_draw_fillrect(win_fb, cur_w, cur_h, cont_x, cy, cont_w, ROW_H - 4, sel_bg);
                    eigen_draw_rect(win_fb, cur_w, cur_h, cont_x, cy, cont_w, ROW_H - 4, accent);
                }

                /* File / Folder icon & name */
                if (filtered[i].is_dir) {
                    eigen_draw_text(win_fb, cur_w, cur_h, cont_x + 8, cy + 6, "[DIR]", folder_clr);
                } else {
                    eigen_draw_text(win_fb, cur_w, cur_h, cont_x + 8, cy + 6, "[FILE]", dim_clr);
                }
                eigen_draw_text(win_fb, cur_w, cur_h, cont_x + 64, cy + 6, filtered[i].name, text_clr);

                /* Size */
                if (!filtered[i].is_dir) {
                    char sz_str[32];
                    format_size(filtered[i].size, sz_str, sizeof(sz_str));
                    eigen_draw_text(win_fb, cur_w, cur_h, cont_x + cont_w - 90, cy + 6, sz_str, dim_clr);
                }
            }
            cy += ROW_H;
        }
    } else {
        /* Grid View */
        for (int i = 0; i < filtered_count; i++) {
            int col = i % GRID_COLS;
            int row = i / GRID_COLS;
            int cx = cont_x + col * (CARD_W + 12);
            int cy = cont_y + row * (CARD_H + 12) - scroll_y;

            if (cy + CARD_H >= cont_y && cy < cont_y + cont_h) {
                eigen_draw_fillrect(win_fb, cur_w, cur_h, cx, cy, CARD_W, CARD_H, (i == selected_idx) ? sel_bg : 0x161B22);
                eigen_draw_rect(win_fb, cur_w, cur_h, cx, cy, CARD_W, CARD_H, (i == selected_idx) ? accent : border_clr);

                if (filtered[i].is_dir) {
                    eigen_draw_text(win_fb, cur_w, cur_h, cx + 24, cy + 20, "[DIR]", folder_clr);
                } else {
                    eigen_draw_text(win_fb, cur_w, cur_h, cx + 20, cy + 20, "[FILE]", dim_clr);
                }

                char card_name[12];
                strncpy(card_name, filtered[i].name, 10);
                card_name[10] = 0;
                eigen_draw_text(win_fb, cur_w, cur_h, cx + 8, cy + 60, card_name, text_clr);
            }
        }
    }

    /* 5. Status Bar */
    int foot_y = cur_h - STATUSBAR_H;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, STATUSBAR_H, sidebar_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, 1, border_clr);

    char stat_msg[128];
    if (selected_idx >= 0 && selected_idx < filtered_count) {
        char sz_str[32];
        format_size(filtered[selected_idx].size, sz_str, sizeof(sz_str));
        snprintf(stat_msg, sizeof(stat_msg), "Selected: %s (%s)", filtered[selected_idx].name, sz_str);
    } else {
        snprintf(stat_msg, sizeof(stat_msg), "%d items in current directory", filtered_count);
    }
    eigen_draw_text(win_fb, cur_w, cur_h, 12, foot_y + 5, stat_msg, dim_clr);

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    win_id = eigen_win_create(80, 60, WIN_W, WIN_H, "File Explorer");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    scan_directory("/");

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

            if (ev->type == EIGEN_EV_MDOWN) {
                int mx = ev->a, my = ev->b;

                /* Search Bar Click */
                if (my >= 10 && my <= 40 && mx >= (int)cur_w - 190 && mx <= (int)cur_w - 10) {
                    search_focus = 1;
                    redraw = 1;
                } else {
                    if (search_focus) { search_focus = 0; redraw = 1; }
                }

                /* View Switcher Click */
                if (my >= 10 && my <= 40 && mx >= (int)cur_w - 260 && mx <= (int)cur_w - 200) {
                    view_mode = !view_mode;
                    redraw = 1;
                }

                /* Back Button */
                if (mx >= 12 && mx <= 42 && my >= 10 && my <= 40) {
                    if (hist_idx > 0) {
                        hist_idx--;
                        scan_directory(history[hist_idx]);
                        redraw = 1;
                    }
                }
                /* Forward Button */
                if (mx >= 48 && mx <= 78 && my >= 10 && my <= 40) {
                    if (hist_idx < hist_count - 1) {
                        hist_idx++;
                        scan_directory(history[hist_idx]);
                        redraw = 1;
                    }
                }

                /* Quick Access Sidebar Click */
                if (mx >= 0 && mx < SIDEBAR_W && my >= TOPBAR_H + 36 && my <= TOPBAR_H + 36 + 5 * 30) {
                    int sidx = (my - (TOPBAR_H + 36)) / 30;
                    if (sidx == 0) scan_directory("/");
                    else if (sidx == 1) scan_directory("user");
                    else if (sidx == 2) scan_directory("wallpapers");
                    else if (sidx == 3) scan_directory("drivers");
                    else if (sidx == 4) scan_directory("vendors");
                    redraw = 1;
                }

                /* Content Area Item Click */
                int cont_x = SIDEBAR_W + 12;
                int cont_y = TOPBAR_H + 8;
                if (mx >= cont_x && my >= cont_y && my < (int)cur_h - STATUSBAR_H) {
                    if (view_mode == 0) {
                        int row = (my - cont_y + scroll_y) / ROW_H;
                        if (row >= 0 && row < filtered_count) {
                            uint32_t now = eigen_gettime_ms();
                            if (selected_idx == row && (now - last_click_time < 400)) {
                                /* Double Click */
                                if (filtered[row].is_dir) {
                                    scan_directory(filtered[row].name);
                                } else {
                                    /* Launch app or file */
                                    eigen_spawn(filtered[row].name);
                                }
                            } else {
                                selected_idx = row;
                                last_click_time = now;
                                last_click_idx = row;
                            }
                            redraw = 1;
                        }
                    }
                }
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;

                if (search_focus) {
                    if (k == '\n' || k == '\r' || code == 0x1C) {
                        search_focus = 0;
                        redraw = 1;
                    } else if (k == 8 || code == 0x0E) { /* Backspace */
                        if (search_len > 0) {
                            search_txt[--search_len] = 0;
                            apply_search_filter();
                            redraw = 1;
                        }
                    } else if (k >= 32 && k < 127 && search_len < 60) {
                        search_txt[search_len++] = k;
                        search_txt[search_len] = 0;
                        apply_search_filter();
                        redraw = 1;
                    }
                } else {
                    if (code == 0x48) { /* Up */
                        if (selected_idx > 0) { selected_idx--; redraw = 1; }
                    } else if (code == 0x50) { /* Down */
                        if (selected_idx < filtered_count - 1) { selected_idx++; redraw = 1; }
                    } else if (code == 0x1C || k == '\n' || k == '\r') { /* Enter */
                        if (selected_idx >= 0 && selected_idx < filtered_count) {
                            if (filtered[selected_idx].is_dir) {
                                scan_directory(filtered[selected_idx].name);
                            } else {
                                eigen_spawn(filtered[selected_idx].name);
                            }
                            redraw = 1;
                        }
                    }
                }
            }
        }

        if (redraw || (search_focus && ((eigen_gettime_ms() / 500) % 2))) {
            render_all();
        }

        eigen_sleep_ms(16);
    }

    eigen_win_close(win_id);
    return 0;
}
