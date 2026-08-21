/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS - Paint Studio (Ring 3) */
#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIN_W 760
#define WIN_H 540
#define MAX_EVS 32
#define SIDEBAR_W 80
#define TOOLBAR_H 40
#define STATUS_H 24
#define CANVAS_BG 0xF8F8F0

static int win_id=-1;
static uint32_t* win_fb=NULL;
static uint32_t cur_w=WIN_W, cur_h=WIN_H;

/* Canvas */
#define CANVAS_MAX (760*540)
static uint32_t canvas_pixels[CANVAS_MAX];
static int canvas_inited=0;

/* Tool state */
static int cur_tool=0; /* 0=pencil 1=eraser */
static int brush_size=4;
static int cur_color_idx=7; /* black */
static int mouse_held=0;
static int prev_mx=-1, prev_my=-1;

static const uint32_t palette[]={
    0xFF3B30,0xFF9500,0xFFCC00,0x34C759,
    0x007AFF,0xAF52DE,0xFFFFFF,0x000000,
    0x8E8E93,0xD1D1D6,0x5AC8FA,0xBF5AF2,
    0x30D158,0xFF6961,0xFFD60A,0x1C1C1E
};

static int canvas_x(void) { return SIDEBAR_W; }
static int canvas_y(void) { return TOOLBAR_H; }
static int canvas_w(void) { return (int)cur_w - SIDEBAR_W; }
static int canvas_h(void) { return (int)cur_h - TOOLBAR_H - STATUS_H; }

static void init_canvas(void) {
    if (!canvas_inited) {
        for (int i=0;i<CANVAS_MAX;i++) canvas_pixels[i]=CANVAS_BG;
        canvas_inited=1;
    }
}

static void paint_pixel(int x, int y) {
    int cw=canvas_w(), ch=canvas_h();
    uint32_t c=(cur_tool==1)?CANVAS_BG:palette[cur_color_idx];
    int r=brush_size/2;
    for (int dy=-r;dy<=r;dy++) {
        for (int dx=-r;dx<=r;dx++) {
            if (dx*dx+dy*dy<=r*r) {
                int px=x+dx, py=y+dy;
                if (px>=0&&px<cw&&py>=0&&py<ch) canvas_pixels[py*cw+px]=c;
            }
        }
    }
}

static void blit_canvas(void) {
    int cx=canvas_x(), cy=canvas_y(), cw=canvas_w(), ch=canvas_h();
    for (int y=0;y<ch&&cy+y<(int)cur_h;y++) {
        for (int x=0;x<cw&&cx+x<(int)cur_w;x++) {
            win_fb[(cy+y)*(int)cur_w+(cx+x)]=canvas_pixels[y*cw+x];
        }
    }
}

static void render_all(void) {
    if (!win_fb) return;
    uint32_t bg=0x161B22, sidebar=0x0D1117, border=0x30363D, text=0xE6EDF3, dim=0x8B949E;

    /* Toolbar */
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,0,cur_w,TOOLBAR_H,bg);
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,TOOLBAR_H-1,cur_w,1,border);
    eigen_draw_text(win_fb,cur_w,cur_h,SIDEBAR_W+8,12,"PAINT STUDIO",dim);

    /* Brush size buttons */
    const int bsizes[]={1,2,4,8};
    const char* bslabels[]={"1","2","4","8"};
    int bstart=SIDEBAR_W+140;
    eigen_draw_text(win_fb,cur_w,cur_h,bstart-50,12,"Brush:",dim);
    for (int i=0;i<4;i++) {
        int bx=bstart+i*30;
        uint32_t bc=(bsizes[i]==brush_size)?0x58A6FF:border;
        eigen_draw_fillrect(win_fb,cur_w,cur_h,bx,6,22,22,bg);
        eigen_draw_rect(win_fb,cur_w,cur_h,bx,6,22,22,bc);
        eigen_draw_text(win_fb,cur_w,cur_h,bx+6,11,bslabels[i],text);
    }

    /* Clear button */
    eigen_draw_fillrect(win_fb,cur_w,cur_h,(int)cur_w-70,8,60,24,0x1F2937);
    eigen_draw_rect(win_fb,cur_w,cur_h,(int)cur_w-70,8,60,24,border);
    eigen_draw_text(win_fb,cur_w,cur_h,(int)cur_w-58,13,"Clear",text);

    /* Left sidebar */
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,0,SIDEBAR_W,cur_h,sidebar);
    eigen_draw_fillrect(win_fb,cur_w,cur_h,SIDEBAR_W-1,0,1,cur_h,border);

    /* Tools */
    eigen_draw_text(win_fb,cur_w,cur_h,6,TOOLBAR_H+8,"TOOLS",dim);
    const char* tools[]={"Pencil","Eraser"};
    for (int t=0;t<2;t++) {
        int ty=TOOLBAR_H+28+t*34;
        uint32_t tc=(t==cur_tool)?0x1F2937:sidebar;
        uint32_t tb=(t==cur_tool)?0x58A6FF:border;
        eigen_draw_fillrect(win_fb,cur_w,cur_h,4,ty,SIDEBAR_W-8,28,tc);
        eigen_draw_rect(win_fb,cur_w,cur_h,4,ty,SIDEBAR_W-8,28,tb);
        eigen_draw_text(win_fb,cur_w,cur_h,10,ty+8,tools[t],text);
    }

    /* Palette */
    eigen_draw_text(win_fb,cur_w,cur_h,6,TOOLBAR_H+108,"COLORS",dim);
    for (int c=0;c<16;c++) {
        int cx=4+(c%2)*34, cy=TOOLBAR_H+124+(c/2)*34;
        eigen_draw_fillrect(win_fb,cur_w,cur_h,cx,cy,28,28,palette[c]);
        if (c==cur_color_idx) eigen_draw_rect(win_fb,cur_w,cur_h,cx,cy,28,28,0xFFFFFF);
        else eigen_draw_rect(win_fb,cur_w,cur_h,cx,cy,28,28,border);
    }

    /* Canvas */
    blit_canvas();

    /* Status bar */
    int sy=(int)cur_h-STATUS_H;
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,sy,cur_w,STATUS_H,bg);
    eigen_draw_fillrect(win_fb,cur_w,cur_h,0,sy,cur_w,1,border);
    char stat[128];
    snprintf(stat,sizeof(stat),"Tool: %s  |  Brush: %dpx  |  Canvas: %dx%d",
        tools[cur_tool], brush_size, canvas_w(), canvas_h());
    eigen_draw_text(win_fb,cur_w,cur_h,SIDEBAR_W+8,sy+5,stat,dim);

    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    (void)argc;(void)argv;
    win_id=eigen_win_create(80,50,WIN_W,WIN_H,"Paint Studio");
    if (win_id<0) return 1;
    win_fb=(uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id,&cur_w,&cur_h);
    init_canvas();

    eigen_ev_t evs[MAX_EVS];
    int running=1;
    while (running) {
        int n=eigen_win_poll(win_id,evs,MAX_EVS);
        for (int i=0;i<n;i++) {
            eigen_ev_t* ev=&evs[i];
            if (ev->type==EIGEN_EV_CLOSE) { running=0; break; }
            if (ev->type==EIGEN_EV_MDOWN) {
                int mx=ev->a, my=ev->b;
                mouse_held=1; prev_mx=mx; prev_my=my;
                /* Toolbar brush size */
                if (my<TOOLBAR_H) {
                    int bstart=SIDEBAR_W+140;
                    const int bsizes[]={1,2,4,8};
                    for (int k=0;k<4;k++) {
                        int bx=bstart+k*30;
                        if (mx>=bx&&mx<bx+22&&my>=6&&my<28) brush_size=bsizes[k];
                    }
                    /* Clear */
                    if (mx>=(int)cur_w-70&&mx<(int)cur_w-10&&my>=8&&my<32) {
                        init_canvas();
                        for (int j=0;j<canvas_w()*canvas_h();j++) canvas_pixels[j]=CANVAS_BG;
                    }
                }
                /* Sidebar tool */
                if (mx<SIDEBAR_W) {
                    for (int t=0;t<2;t++) {
                        int ty=TOOLBAR_H+28+t*34;
                        if (my>=ty&&my<ty+28) cur_tool=t;
                    }
                    /* Palette */
                    for (int c=0;c<16;c++) {
                        int pcx=4+(c%2)*34, pcy=TOOLBAR_H+124+(c/2)*34;
                        if (mx>=pcx&&mx<pcx+28&&my>=pcy&&my<pcy+28) cur_color_idx=c;
                    }
                }
                /* Canvas draw */
                if (mx>=canvas_x()&&my>=canvas_y()&&my<canvas_y()+canvas_h()) {
                    paint_pixel(mx-canvas_x(),my-canvas_y());
                }
            }
            if (ev->type==EIGEN_EV_MUP) { mouse_held=0; prev_mx=-1; prev_my=-1; }
            if (ev->type==EIGEN_EV_MMOVE&&mouse_held) {
                int mx=ev->a, my=ev->b;
                if (mx>=canvas_x()&&my>=canvas_y()&&my<canvas_y()+canvas_h()) {
                    paint_pixel(mx-canvas_x(),my-canvas_y());
                }
                prev_mx=mx; prev_my=my;
            }
        }
        render_all();
        eigen_sleep_ms(16);
    }
    eigen_win_close(win_id);
    return 0;
}
