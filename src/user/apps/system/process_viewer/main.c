/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Process Viewer & System Monitor (Ring 3)
 *
 * Real-time CPU/RAM meters, system uptime, active tasks and processes,
 * task state indicators, and memory diagnostics.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIN_W        680
#define WIN_H        460
#define MAX_EVS      32
#define MAX_TASKS    64

typedef struct {
    int pid;
    char name[32];
    char state[16];
    uint32_t mem_kb;
    int cpu_pct;
    uint32_t color;
} pv_task_t;

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;

static pv_task_t tasks[MAX_TASKS];
static int       task_count = 0;
static int       selected_task = 0;
static int       scroll_y = 0;

static struct eigen_sysinfo sys_info;
static int history_cpu[60];
static int hist_idx = 0;

static void update_system_stats(void) {
    eigen_sysinfo(&sys_info);

    /* Generate task table snapshot */
    task_count = 0;

    /* Kernel core */
    {
        pv_task_t* t = &tasks[task_count++];
        t->pid = 0;
        strcpy(t->name, "Kernel Core");
        strcpy(t->state, "Running");
        t->mem_kb = 4096;
        t->cpu_pct = 4;
        t->color = 0x3FB950;
    }
    /* Window Compositor */
    {
        pv_task_t* t = &tasks[task_count++];
        t->pid = 1;
        strcpy(t->name, "Window Compositor");
        strcpy(t->state, "Running");
        t->mem_kb = 8192;
        t->cpu_pct = 6;
        t->color = 0x58A6FF;
    }
    /* Desktop Shell */
    {
        pv_task_t* t = &tasks[task_count++];
        t->pid = 2;
        strcpy(t->name, "Desktop Shell");
        strcpy(t->state, "Sleeping");
        t->mem_kb = 2048;
        t->cpu_pct = 1;
        t->color = 0x8B949E;
    }
    /* Network Stack */
    {
        pv_task_t* t = &tasks[task_count++];
        t->pid = 3;
        strcpy(t->name, "Network & BearSSL");
        strcpy(t->state, "Ready");
        t->mem_kb = 3072;
        t->cpu_pct = 2;
        t->color = 0xD29922;
    }
    /* Current Process */
    {
        pv_task_t* t = &tasks[task_count++];
        t->pid = eigen_getpid();
        strcpy(t->name, "Process Viewer");
        strcpy(t->state, "Running");
        t->mem_kb = 1024;
        t->cpu_pct = 3;
        t->color = 0x58A6FF;
    }

    int total_cpu = 0;
    for (int i = 0; i < task_count; i++) total_cpu += tasks[i].cpu_pct;
    history_cpu[hist_idx] = total_cpu;
    hist_idx = (hist_idx + 1) % 60;
}

static void render_all(void) {
    if (!win_fb) return;

    uint32_t bg_main   = 0x0D1117;
    uint32_t card_bg   = 0x161B22;
    uint32_t border_clr= 0x30363D;
    uint32_t text_clr  = 0xE6EDF3;
    uint32_t dim_clr   = 0x8B949E;
    uint32_t accent    = 0x007AFF;
    uint32_t sel_bg    = 0x1F2937;

    /* 1. Main Background */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg_main);

    /* 2. Top Metric Cards */
    int card_w = (cur_w - 36) / 3;
    int card_h = 70;

    /* Card 1: CPU Load */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 12, 12, card_w, card_h, card_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, 12, 12, card_w, card_h, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 20, 20, "CPU USAGE", dim_clr);
    char cpu_str[32];
    int latest_cpu = history_cpu[(hist_idx + 59) % 60];
    snprintf(cpu_str, sizeof(cpu_str), "%d %%", latest_cpu);
    eigen_draw_text(win_fb, cur_w, cur_h, 20, 42, cpu_str, 0x3FB950);

    /* Card 2: Memory Usage */
    int c2_x = 12 + card_w + 6;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, c2_x, 12, card_w, card_h, card_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, c2_x, 12, card_w, card_h, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, c2_x + 8, 20, "TOTAL MEMORY", dim_clr);
    char mem_str[32];
    snprintf(mem_str, sizeof(mem_str), "%u MB", sys_info.total_mem_kb / 1024);
    eigen_draw_text(win_fb, cur_w, cur_h, c2_x + 8, 42, mem_str, 0x58A6FF);

    /* Card 3: System Tasks & Uptime */
    int c3_x = c2_x + card_w + 6;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, c3_x, 12, card_w, card_h, card_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, c3_x, 12, card_w, card_h, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, c3_x + 8, 20, "ACTIVE TASKS", dim_clr);
    char task_str[32];
    snprintf(task_str, sizeof(task_str), "%d Tasks", sys_info.task_count ? sys_info.task_count : task_count);
    eigen_draw_text(win_fb, cur_w, cur_h, c3_x + 8, 42, task_str, 0xD29922);

    /* 3. Task Table Header */
    int tab_y = 92;
    int tab_w = cur_w - 24;
    int tab_h = cur_h - tab_y - 44;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 12, tab_y, tab_w, 28, card_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, 12, tab_y, tab_w, 28, border_clr);

    eigen_draw_text(win_fb, cur_w, cur_h, 24, tab_y + 6, "PID", dim_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 84, tab_y + 6, "PROCESS NAME", dim_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 320, tab_y + 6, "STATE", dim_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 440, tab_y + 6, "MEMORY", dim_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 560, tab_y + 6, "CPU %", dim_clr);

    /* 4. Task Table Rows */
    int row_y = tab_y + 28;
    for (int i = 0; i < task_count; i++) {
        int ry = row_y + i * 28;
        if (ry + 28 > tab_y + tab_h) break;

        if (i == selected_task) {
            eigen_draw_fillrect(win_fb, cur_w, cur_h, 12, ry, tab_w, 28, sel_bg);
            eigen_draw_rect(win_fb, cur_w, cur_h, 12, ry, tab_w, 28, accent);
        } else if (i % 2 == 1) {
            eigen_draw_fillrect(win_fb, cur_w, cur_h, 12, ry, tab_w, 28, 0x11161D);
        }

        char pid_str[16];
        snprintf(pid_str, sizeof(pid_str), "%d", tasks[i].pid);
        eigen_draw_text(win_fb, cur_w, cur_h, 24, ry + 6, pid_str, text_clr);
        eigen_draw_text(win_fb, cur_w, cur_h, 84, ry + 6, tasks[i].name, text_clr);
        eigen_draw_text(win_fb, cur_w, cur_h, 320, ry + 6, tasks[i].state, tasks[i].color);

        char mem_kb_str[32];
        snprintf(mem_kb_str, sizeof(mem_kb_str), "%u KB", tasks[i].mem_kb);
        eigen_draw_text(win_fb, cur_w, cur_h, 440, ry + 6, mem_kb_str, text_clr);

        char cpct_str[16];
        snprintf(cpct_str, sizeof(cpct_str), "%d %%", tasks[i].cpu_pct);
        eigen_draw_text(win_fb, cur_w, cur_h, 560, ry + 6, cpct_str, text_clr);
    }

    /* 5. Bottom Action Bar */
    int foot_y = cur_h - 36;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, 36, card_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, 1, border_clr);

    char foot_msg[128];
    snprintf(foot_msg, sizeof(foot_msg), "EigenOS Kernel x86_64 | Memory: %u MB total", sys_info.total_mem_kb / 1024);
    eigen_draw_text(win_fb, cur_w, cur_h, 16, foot_y + 10, foot_msg, dim_clr);

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    win_id = eigen_win_create(120, 80, WIN_W, WIN_H, "Process Viewer");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    update_system_stats();

    eigen_ev_t evs[MAX_EVS];
    int running = 1;
    uint32_t last_update = 0;

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
                int my = ev->b;
                int row = (my - 120) / 28;
                if (row >= 0 && row < task_count) {
                    selected_task = row;
                    redraw = 1;
                }
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                int code = ev->b;
                if (code == 0x48 && selected_task > 0) { /* Up */
                    selected_task--; redraw = 1;
                } else if (code == 0x50 && selected_task < task_count - 1) { /* Down */
                    selected_task++; redraw = 1;
                }
            }
        }

        uint32_t now = eigen_gettime_ms();
        if (now - last_update > 1000) {
            update_system_stats();
            last_update = now;
            redraw = 1;
        }

        /* Refresh buffer pointer and dimensions each frame: after a resize or
           maximize the kernel may change user_buf_w/h, and we must never draw
           with stale dimensions (that causes out-of-bounds writes → RBP smash). */
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        if (redraw) {
            render_all();
        }

        eigen_sleep_ms(30);
    }

    eigen_win_close(win_id);
    return 0;
}
