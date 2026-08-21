/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
#include "userlib.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* EigenOS Terminal — pure text shell (ring 3).
 * The prompt lives on the LAST line of the scrollback, like a real
 * terminal: output pushes it up, PageUp/PageDown scroll history, the
 * prompt stays anchored at the bottom while editing. */

#define WIN_W      760
#define WIN_H      500
#define MAX_EVS    64
#define TERM_ROWS  512
#define TERM_COLS  128
#define HIST_SIZE  64

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;

static char    buf_text[TERM_ROWS][TERM_COLS];
static uint32_t buf_color[TERM_ROWS];
static int     buf_head = 0;
static int     buf_count = 0;
static int     scroll_off = 0;    /* lines scrolled back (0 = newest) */

static char    input[256];
static int     input_len = 0;

static char    history[HIST_SIZE][256];
static int     hist_count = 0;
static int     hist_nav = -1;

static char    prompt[96] = "eigen@eigenos:/ $ ";
static uint32_t last_blink_ms = 0;
static int     cursor_visible = 1;

/* palette */
static const uint32_t C_OUT   = 0xE6EDF3;   /* default output   */
static const uint32_t C_DIR   = 0x58A6FF;   /* directories      */
static const uint32_t C_ACC   = 0x7AA2F7;   /* prompts / info   */
static const uint32_t C_OK    = 0x3FB950;   /* success          */
static const uint32_t C_WARN  = 0xD29922;   /* warnings         */
static const uint32_t C_ERR   = 0xF85149;   /* errors           */
static const uint32_t C_DIM   = 0x8B949E;   /* faint            */

/* ── scrollback ─────────────────────────────────────────────── */
static void buf_push(const char* text, uint32_t color) {
    int row = buf_head % TERM_ROWS;
    int n = 0;
    while (text[n] && n < TERM_COLS - 1) { buf_text[row][n] = text[n]; n++; }
    buf_text[row][n] = 0;
    buf_color[row] = color;
    buf_head++;
    if (buf_count < TERM_ROWS) buf_count++;
    scroll_off = 0;
}

/* push possibly multi-line output; long lines are wrapped */
static void push_lines(const char* text, uint32_t color) {
    if (!text || !*text) { buf_push("", color); return; }
    char line[TERM_COLS];
    int  li = 0;
    const char* p = text;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            line[li] = 0;
            buf_push(line, color);
            li = 0;
            p++;
            while (*p == '\r' || *p == '\n') p++;
            continue;
        }
        if ((unsigned char)*p < 32 && *p != '\t') { p++; continue; }  /* strip control */
        if (li >= TERM_COLS - 1) { line[li] = 0; buf_push(line, color); li = 0; }
        line[li++] = *p++;
    }
    if (li > 0) { line[li] = 0; buf_push(line, color); }
}

/* strip \e[...m ANSI codes from the kernel's coloured fs_list output */
static int strip_ansi(char* dst, const char* src, int max) {
    int di = 0;
    for (int si = 0; src[si] && di < max - 1; si++) {
        if (src[si] == '\e') {
            si++;
            while (src[si] && src[si] != 'm') si++;
            continue;
        }
        dst[di++] = src[si];
    }
    dst[di] = 0;
    return di;
}

static void update_prompt(void) {
    char cwd[128];
    if (eigen_fs_pwd(cwd, sizeof(cwd)) == 0 && cwd[0]) {
        int p = 0;
        const char* pre = "eigen@eigenos:";
        while (*pre && p < 60) prompt[p++] = *pre++;
        int cl = 0; while (cwd[cl]) cl++;
        int start = 0;
        if (cl > 22) start = cl - 22;
        for (int i = start; i < cl && p < 88; i++) prompt[p++] = cwd[i];
        const char* suf = "$ ";
        while (*suf && p < 95) prompt[p++] = *suf++;
        prompt[p] = 0;
    }
}

/* split cmd into whitespace-separated args (max 4) */
static int split_args(const char* cmd, char args[][128]) {
    int n = 0, i = 0;
    while (cmd[i] && n < 4) {
        while (cmd[i] == ' ' || cmd[i] == '\t') i++;
        if (!cmd[i]) break;
        int a = 0;
        while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t' && a < 126) args[n][a++] = cmd[i++];
        args[n][a] = 0;
        n++;
    }
    return n;
}

static void cmd_ls(void) {
    char raw[4096];
    int len = eigen_fs_list(raw, sizeof(raw));
    if (len <= 0) { buf_push("ls: nothing here", C_DIM); return; }
    char line[TERM_COLS];
    int li = 0, items = 0;
    for (int i = 0; i < len && raw[i]; i++) {
        if (raw[i] == '\n') {
            line[li] = 0;
            if (line[0]) {
                char clean[128];
                strip_ansi(clean, line, sizeof(clean));
                int last = 0; while (clean[last]) last++;
                buf_push(clean, last > 0 && clean[last - 1] == '/' ? C_DIR : C_OUT);
                items++;
            }
            li = 0;
            continue;
        }
        if (li < TERM_COLS - 1) line[li++] = raw[i];
    }
    if (items == 0) buf_push("ls: nothing here", C_DIM);
}

static void cmd_cat(const char* file) {
    if (!file[0]) { buf_push("cat: missing operand", C_ERR); return; }
    static char data[4096];
    int n = eigen_fs_read_file(file, data, sizeof(data) - 1);
    if (n < 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "cat: %s: no such file", file);
        buf_push(msg, C_ERR);
        return;
    }
    data[n] = 0;
    push_lines(data, C_OUT);
}

static void cmd_exec(const char* cmd) {
    if (hist_count < HIST_SIZE) strncpy(history[hist_count++], cmd, 255);
    hist_nav = -1;

    char echo_line[300];
    snprintf(echo_line, sizeof(echo_line), "%s%s", prompt, cmd);
    buf_push(echo_line, C_OK);

    char args[4][128];
    int argc = split_args(cmd, args);
    if (argc == 0) return;
    const char* c = args[0];

    if (strcmp(c, "clear") == 0) { buf_head = 0; buf_count = 0; scroll_off = 0; return; }
    if (strcmp(c, "help") == 0) {
        buf_push("Commands:", C_ACC);
        buf_push("  ls               list files in the current directory", C_OUT);
        buf_push("  cd <path>        change directory ('cd /' = root)", C_OUT);
        buf_push("  pwd              print the current directory", C_OUT);
        buf_push("  cat <file>       print a text file", C_OUT);
        buf_push("  echo <text>      print text", C_OUT);
        buf_push("  touch <file>     create an empty file", C_OUT);
        buf_push("  rm <file>        delete a file", C_OUT);
        buf_push("  mkdir <dir>      create a directory", C_OUT);
        buf_push("  rmdir <dir>      remove a directory", C_OUT);
        buf_push("  mv <src> <dst>   rename / move", C_OUT);
        buf_push("  truncate <file>  empty an existing file", C_OUT);
        buf_push("  sysinfo          system information", C_OUT);
        buf_push("  time / date      current time and date", C_OUT);
        buf_push("  clear            clear the screen", C_OUT);
        buf_push("Apps: edim, file_explorer, settings, calculator, clock,", C_DIM);
        buf_push("      calendar, weather, hexdump, process_viewer, doom, awk", C_DIM);
        return;
    }
    if (strcmp(c, "ls") == 0) { cmd_ls(); return; }
    if (strcmp(c, "pwd") == 0) {
        char cwd[128];
        eigen_fs_pwd(cwd, sizeof(cwd));
        buf_push(cwd[0] ? cwd : "/", C_OUT);
        return;
    }
    if (strcmp(c, "cd") == 0) {
        if (argc < 2) { update_prompt(); return; }
        if (eigen_fs_chdir(args[1]) == 0) update_prompt();
        else {
            char msg[160];
            snprintf(msg, sizeof(msg), "cd: %s: no such directory", args[1]);
            buf_push(msg, C_ERR);
        }
        return;
    }
    if (strcmp(c, "cat") == 0) { cmd_cat(argc > 1 ? args[1] : ""); return; }
    if (strcmp(c, "echo") == 0) {
        char out[TERM_COLS];
        int o = 0;
        for (int i = 1; i < argc && o < TERM_COLS - 2; i++) {
            int k = 0; while (args[i][k] && o < TERM_COLS - 2) out[o++] = args[i][k++];
            if (i < argc - 1 && o < TERM_COLS - 1) out[o++] = ' ';
        }
        out[o] = 0;
        buf_push(out, C_OUT);
        return;
    }
    if (strcmp(c, "touch") == 0) {
        if (argc < 2) { buf_push("touch: missing operand", C_ERR); return; }
        buf_push(eigen_fs_create(args[1]) == 0 ? "created" : "touch: could not create", C_OK);
        return;
    }
    if (strcmp(c, "rm") == 0) {
        if (argc < 2) { buf_push("rm: missing operand", C_ERR); return; }
        buf_push(eigen_fs_delete(args[1]) == 0 ? "deleted" : "rm: could not delete", C_OK);
        return;
    }
    if (strcmp(c, "mkdir") == 0) {
        if (argc < 2) { buf_push("mkdir: missing operand", C_ERR); return; }
        buf_push(eigen_mkdir(args[1]) == 0 ? "created" : "mkdir: could not create", C_OK);
        return;
    }
    if (strcmp(c, "rmdir") == 0) {
        if (argc < 2) { buf_push("rmdir: missing operand", C_ERR); return; }
        buf_push(eigen_fs_rmdir(args[1]) == 0 ? "removed" : "rmdir: could not remove", C_OK);
        return;
    }
    if (strcmp(c, "truncate") == 0) {
        if (argc < 2) { buf_push("truncate: missing operand", C_ERR); return; }
        buf_push(eigen_fs_truncate(args[1]) == 0 ? "truncated" : "truncate: no such file", C_OK);
        return;
    }
    if (strcmp(c, "mv") == 0) {
        if (argc < 3) { buf_push("mv: missing operand", C_ERR); return; }
        buf_push(eigen_fs_rename(args[1], args[2]) == 0 ? "moved" : "mv: could not move", C_OK);
        return;
    }
    if (strcmp(c, "sysinfo") == 0) {
        struct eigen_sysinfo si;
        if (eigen_sysinfo(&si) == 0) {
            char line[TERM_COLS];
            snprintf(line, sizeof(line), "EigenOS x86_64  |  api v%u  |  %u MB RAM  |  %u tasks",
                     si.api_version, si.total_mem_kb / 1024, si.task_count);
            buf_push(line, C_ACC);
            snprintf(line, sizeof(line), "Screen %ux%u  |  uptime %u ms  |  timer %u Hz",
                     si.screen_w, si.screen_h, si.uptime_ms, si.timer_hz);
            buf_push(line, C_DIM);
        }
        return;
    }
    if (strcmp(c, "time") == 0 || strcmp(c, "date") == 0) {
        int t[6];
        eigen_time_get(t);
        char line[64];
        snprintf(line, sizeof(line), "%02d:%02d:%02d  (RTC)", t[0], t[1], t[2]);
        buf_push(line, C_OUT);
        return;
    }
    if (strcmp(c, "uname") == 0 || strcmp(c, "version") == 0) {
        buf_push("EigenOS x86_64 | ring-3 shell | Copyright Sidney 2024-2026", C_ACC);
        return;
    }

    const char* apps[] = {
        "edim","file_explorer","edrowser","calculator","settings",
        "weather","hexdump","process_viewer","clock","calendar",
        "doom","awk","posixtest","ftglyph","pthreadtest","setjmptest","polltest","eintest","eotest","zlibtest","eettest","pngtest","jpegtest","ecoretest",NULL
    };
    for (int i = 0; apps[i]; i++) {
        if (strcmp(c, apps[i]) == 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Launching %s...", c);
            buf_push(msg, C_WARN);
            eigen_spawn(c);
            return;
        }
    }

    char err[128];
    snprintf(err, sizeof(err), "bash: %s: command not found", c);
    buf_push(err, C_ERR);
}

/* ── rendering ───────────────────────────────────────────────── */
static void render_all(void) {
    if (!win_fb) return;
    uint32_t bg = 0x0C0C0C;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg);

    int row_h = 17, x0 = 10, y0 = 8, pad = 6;
    int avail = (int)cur_h > y0 + pad + row_h ? (int)(cur_h - y0 - pad) / row_h : 2;
    int hist_rows = avail - 1;              /* last row is the prompt */
    int first = buf_count - hist_rows - scroll_off;
    if (first < 0) first = 0;

    for (int r = 0; r < hist_rows; r++) {
        int ri = first + r;
        if (ri >= buf_count) break;
        int bidx = (buf_head - buf_count + ri + TERM_ROWS) % TERM_ROWS;
        eigen_draw_text(win_fb, cur_w, cur_h, x0, y0 + r * row_h,
                        buf_text[bidx], buf_color[bidx]);
    }

    int iy = y0 + hist_rows * row_h;
    eigen_draw_text(win_fb, cur_w, cur_h, x0, iy, prompt, C_OK);
    int px = x0 + (int)strlen(prompt) * 8;
    eigen_draw_text(win_fb, cur_w, cur_h, px, iy, input, C_OUT);

    /* Thin blinking text cursor — the prompt line is plain terminal text,
       not an input bar, so the cursor is a slim vertical line like a real
       terminal's, never a chunky block. */
    uint32_t now = eigen_gettime_ms();
    if (now - last_blink_ms > 500) { cursor_visible ^= 1; last_blink_ms = now; }
    if (cursor_visible) {
        int cx0 = px + input_len * 8;
        eigen_draw_fillrect(win_fb, cur_w, cur_h, cx0, iy, 2, row_h, C_OK);
    }

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(80, 60, WIN_W, WIN_H, "Terminal");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    update_prompt();
    buf_push("EigenOS Terminal  --  type 'help' for commands", C_ACC);
    buf_push("", C_OUT);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;
    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) { running = 0; break; }
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                int code = ev->b;
                char ch = (char)ev->a;

                if (code == 0x0E) {                    /* Backspace */
                    if (input_len > 0) input[--input_len] = 0;
                } else if (ch == '\n' || ch == '\r' || code == 0x1C) {
                    input[input_len] = 0;
                    cmd_exec(input);
                    input_len = 0; input[0] = 0;
                } else if (code == 0x48) {             /* Up: history */
                    if (hist_count > 0) {
                        if (hist_nav < hist_count - 1) hist_nav++;
                        strncpy(input, history[hist_count - 1 - hist_nav], 255);
                        input_len = (int)strlen(input);
                    }
                } else if (code == 0x50) {             /* Down: history */
                    if (hist_nav > 0) {
                        hist_nav--;
                        strncpy(input, history[hist_count - 1 - hist_nav], 255);
                        input_len = (int)strlen(input);
                    } else {
                        hist_nav = -1; input[0] = 0; input_len = 0;
                    }
                } else if (code == 0x49) {             /* PageUp: scroll back */
                    scroll_off += 6;
                    if (scroll_off > buf_count) scroll_off = buf_count;
                } else if (code == 0x51) {             /* PageDown: scroll fwd */
                    scroll_off -= 6;
                    if (scroll_off < 0) scroll_off = 0;
                } else if (ch == 27) {                 /* Esc: clear the line */
                    input[0] = 0; input_len = 0;
                } else if (ch >= 32 && ch < 127 && input_len < 255) {
                    input[input_len++] = ch;
                    input[input_len] = 0;
                }
            }
        }
        render_all();
        eigen_sleep_ms(30);
    }
    eigen_win_close(win_id);
    return 0;
}
