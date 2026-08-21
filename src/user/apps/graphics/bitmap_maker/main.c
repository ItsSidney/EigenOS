/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS - Bitmap Maker / Pixel Editor (Ring 3) */
#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIN_W 640
#define WIN_H 480
#define MAX_EVS 32
#define SIDEBAR_W 80
#define TOOLBAR_H 40
#define STATUS_H 24

#define GRID_MAX 32
static int grid_w=16, grid_h=16;
static uint32_t grid_pixels[GRID_MAX*GRID_MAX];

static int win_id=-1;
static uint32_t* win_fb=NULL;
static uint32_t cur_w=WIN_W, cur_h=WIN_H;
static int cur_color_idx=15; /* black */

static const uint32_t palette[]={
    0xFFFFFF,0xD1D1D6,0x8E8E93,0x636366,
    0x1C1C1E,0x000000,0xFF3B30,0xFF9500,
    0xFFCC00,0x34C759,0x007AFF,0xAF52DE,
    0xFF6961,0x30D158,0x5AC8FA,0xFFD60A
};

static void init_grid(void) {
    for (int i=0;i<GRID_MAX*GRID_MAX;i++) grid_pixels[i]=0xFFFFFF;
}

static int canvas_x(void) { return SIDEBAR_W+8; }
static int canvas_y(void) { return TOOLBAR_H+8; }
static int canvas_size(void) {
    int avail_w=(int)cur_w-SIDEBAR_W-16;
    int avail_h=(int)cur_h-TOOLBAR_H-STATUS_H-16;
    if (avail_w <= 0 || avail_h <= 0 || grid_w <= 0 || grid_h <= 0) return 8;
    int cell_w=avail_w/grid_w;
    int cell_h=avail_h/grid_h;
    int cs = (cell_w<cell_h)?cell_w:cell_h;
    return (cs > 0) ? cs : 8;
}

static void render_all(void) {
    if (!win_fb) return;
    uint32_t bg=0x0D1117, sidebar=0x111418, border=0x2D3748, text=0xE6EDF3, dim=0x8B949E;

    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,0,cur_w,cur_h,bg);

    /* Toolbar */
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,0,cur_w,TOOLBAR_H,0x161B22);
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,TOOLBAR_H-1,cur_w,1,border);
    eigen_draw_text(win_fb,cur_w,cur_h,SIDEBAR_W+14,12,"BITMAP MAKER",dim);

    /* Grid size buttons */
    const int gsizes[]={8,16,32};
    const char* gslabels[]={"8x8","16x16","32x32"};
    int gstart=SIDEBAR_W+160;
    eigen_draw_text(win_fb,cur_w,cur_h,gstart-56,12,"Grid:",dim);
    for (int g=0;g<3;g++) {
        int gx=gstart+g*68;
        int isActive=(gsizes[g]==grid_w);
        uint32_t gc=isActive?0x007AFF:border;
        eigen_draw_fillrect(win_fb,cur_w,cur_h,gx,6,58,24,bg);
        eigen_draw_rect(win_fb,cur_w,cur_h,gx,6,58,24,gc);
        eigen_draw_text(win_fb,cur_w,cur_h,gx+8,12,gslabels[g],isActive?0xFFFFFF:text);
    }

    /* Clear */
    eigen_draw_fillrect(win_fb,cur_w,cur_h,(int)cur_w-70,8,60,24,0x1F2937);
    eigen_draw_rect(win_fb,cur_w,cur_h,(int)cur_w-70,8,60,24,border);
    eigen_draw_text(win_fb,cur_w,cur_h,(int)cur_w-58,13,"Clear",text);

    /* Sidebar palette */
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,0,SIDEBAR_W,cur_h,sidebar);
    eigen_draw_fillrect(win_fb,cur_w,cur_h,SIDEBAR_W-1,0,1,cur_h,border);
    eigen_draw_text(win_fb,cur_w,cur_h,6,TOOLBAR_H+8,"COLORS",dim);
    for (int c=0;c<16;c++) {
        int px=4+(c%2)*36, py=TOOLBAR_H+24+(c/2)*36;
        eigen_draw_fillrect(win_fb,cur_w,cur_h,px,py,30,30,palette[c]);
        eigen_draw_rect(win_fb,cur_w,cur_h,px,py,30,30,(c==cur_color_idx)?0xFFFFFF:border);
    }

    /* Grid cells */
    int cs=canvas_size();
    int gx0=canvas_x(), gy0=canvas_y();
    for (int r=0;r<grid_h;r++) {
        for (int c=0;c<grid_w;c++) {
            int px=gx0+c*cs, py=gy0+r*cs;
            eigen_draw_fillrect(win_fb,cur_w,cur_h,px,py,cs,cs,grid_pixels[r*grid_w+c]);
            eigen_draw_rect(win_fb,cur_w,cur_h,px,py,cs,cs,border);
        }
    }

    /* Status bar */
    int sy=(int)cur_h-STATUS_H;
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,sy,cur_w,STATUS_H,0x161B22);
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,sy,cur_w,1,border);
    char stat[64];
    snprintf(stat,sizeof(stat),"Canvas: %dx%d  |  Cell size: %dpx",grid_w,grid_h,cs);
    eigen_draw_text(win_fb,cur_w,cur_h,SIDEBAR_W+8,sy+5,stat,dim);

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    (void)argc;(void)argv;
    win_id=eigen_win_create(90,60,WIN_W,WIN_H,"Bitmap Maker");
    if (win_id<0) return 1;
    win_fb=(uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id,&cur_w,&cur_h);
    init_grid();

    eigen_ev_t evs[MAX_EVS];
    int running=1;
    while (running) {
        eigen_win_getsize(win_id,&cur_w,&cur_h);
        int n=eigen_win_poll(win_id,evs,MAX_EVS);
        for (int i=0;i<n;i++) {
            eigen_ev_t* ev=&evs[i];
            if (ev->type==EIGEN_EV_CLOSE) { running=0; break; }
            if (ev->type==EIGEN_EV_MDOWN) {
                int mx=ev->a, my=ev->b;
                /* Toolbar grid size */
                if (my<TOOLBAR_H) {
                    const int gsizes[]={8,16,32};
                    int gstart=SIDEBAR_W+160;
                    for (int g=0;g<3;g++) {
                        int gx=gstart+g*68;
                        if (mx>=gx&&mx<gx+58&&my>=6&&my<30) {
                            grid_w=gsizes[g]; grid_h=gsizes[g]; init_grid();
                        }
                    }
                    if (mx>=(int)cur_w-70&&my>=8&&my<32) init_grid();
                }
                /* Palette */
                if (mx<SIDEBAR_W&&my>TOOLBAR_H) {
                    for (int c=0;c<16;c++) {
                        int px=4+(c%2)*36, py=TOOLBAR_H+24+(c/2)*36;
                        if (mx>=px&&mx<px+30&&my>=py&&my<py+30) cur_color_idx=c;
                    }
                }
                /* Grid */
                int cs=canvas_size(), gx0=canvas_x(), gy0=canvas_y();
                if (cs > 0) {
                    int gc=(mx-gx0)/cs, gr=(my-gy0)/cs;
                    if (gc>=0&&gc<grid_w&&gr>=0&&gr<grid_h) {
                        grid_pixels[gr*grid_w+gc]=palette[cur_color_idx];
                    }
                }
            }
        }
        render_all();
        eigen_sleep_ms(30);
    }
    eigen_win_close(win_id);
    return 0;
}
