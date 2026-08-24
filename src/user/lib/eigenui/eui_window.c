/***************************************************************/
/*  EigenUI Window — implementation                              */
/***************************************************************/
#include "eui_window.h"
#include "eui_layout.h"
#include "eui_font.h"
#include "eui_anim.h"
#include "userlib.h"
#include <stdlib.h>
#include <string.h>

struct eui_window {
    int         id;
    uint32_t*   buf;
    int         buf_w, buf_h;
    eui_canvas  canvas;
    eui_theme*  theme;
    eui_widget* root;
    int         dirty;
    int         mx, my;
    eui_widget* hovered;
    eui_widget* focused;
    eui_widget* capture;
    int         running;
    float       scale;
};

static int g_font_ready = 0;

eui_window* eui_window_new(const char* title, int w, int h) {
    if (!g_font_ready) { eui_font_init(); g_font_ready = 1; }
    eui_window* win = (eui_window*)eigen_malloc(sizeof(eui_window));
    memset(win, 0, sizeof(*win));
    win->id = eigen_win_create(0, 0, w, h, title ? title : "EigenUI");
    win->buf = (uint32_t*)eigen_win_map(win->id);
    uint32_t bw = 0, bh = 0; eigen_win_getsize(win->id, &bw, &bh);
    win->buf_w = (int)bw; win->buf_h = (int)bh;
    win->canvas.buf = win->buf;
    win->canvas.w = win->buf_w; win->canvas.h = win->buf_h;
    win->canvas.scale = 1.0f; win->canvas.clip_sp = 0;
    win->scale = 1.0f;
    win->theme = (eui_theme*)eigen_malloc(sizeof(eui_theme));
    *win->theme = *eui_theme_default();
    win->root = eui_box_new(1);          /* vertical root */
    eui_box_set_padding(win->root, 10);
    win->dirty = 1;
    win->running = 1;
    eui_window_use_os_theme(win);
    return win;
}

void eui_window_free(eui_window* w) {
    if (!w) return;
    if (w->root) eui_widget_free(w->root);
    if (w->theme) eigen_free(w->theme);
    eigen_win_close(w->id);
    eigen_free(w);
}
void eui_window_set_title(eui_window* w, const char* t) { if (w) eigen_win_settitle(w->id, t); }
void eui_window_set_theme(eui_window* w, const eui_theme* t) { if (w && t) { *w->theme = *t; w->dirty = 1; } }
void eui_window_set_scale(eui_window* w, float s) { if (w) { w->scale = s > 0 ? s : 1.0f; w->canvas.scale = w->scale; w->dirty = 1; } }

void eui_window_use_os_theme(eui_window* w) {
    uint32_t arr[EIGEN_THEME_COUNT];
    int n = eigen_win_gettheme(arr, EIGEN_THEME_COUNT);
    if (n > 0) { eui_theme_from_os(w->theme, arr, n); w->dirty = 1; }
}

eui_widget* eui_window_root(eui_window* w) { return w ? w->root : 0; }
void eui_window_add(eui_window* w, eui_widget* child) {
    if (w && child) { eui_box_add(w->root, child); w->dirty = 1; }
}
void eui_window_set_content(eui_window* w, eui_widget* root) {
    if (!w || !root) return;
    w->root = root; w->dirty = 1;
}
void eui_window_mark_dirty(eui_window* w) { if (w) w->dirty = 1; }

static void eui_window_handle(eui_window* w, const eigen_ev_t* ev) {
    switch (ev->type) {
    case EIGEN_EV_MMOVE: {
        w->mx = ev->a; w->my = ev->b;
        eui_event e; memset(&e, 0, sizeof(e));
        e.type = EUI_EV_MOUSEMOVE; e.x = ev->a; e.y = ev->b;
        eui_widget* hit = eui_widget_hit(w->root, ev->a, ev->b);
        if (hit != w->hovered) {
            if (w->hovered) w->hovered->hovered = 0;
            w->hovered = hit;
            if (hit) hit->hovered = 1;
        }
        if (w->capture) eui_widget_event(w->capture, &e, w);
        else if (hit) eui_widget_event(hit, &e, w);
        w->dirty = 1;
        break;
    }
    case EIGEN_EV_MDOWN: {
        eui_event e; memset(&e, 0, sizeof(e));
        e.type = EUI_EV_MOUSEDOWN; e.x = ev->a; e.y = ev->b; e.button = ev->c;
        eui_widget* hit = eui_widget_hit(w->root, ev->a, ev->b);
        w->capture = (hit && hit->enabled) ? hit : 0;
        if (hit && hit->focusable) w->focused = hit;
        if (w->capture) eui_widget_event(w->capture, &e, w);
        w->dirty = 1;
        break;
    }
    case EIGEN_EV_MUP: {
        eui_event e; memset(&e, 0, sizeof(e));
        e.type = EUI_EV_MOUSEUP; e.x = ev->a; e.y = ev->b; e.button = ev->c;
        if (w->capture) { eui_widget_event(w->capture, &e, w); w->capture = 0; }
        w->dirty = 1;
        break;
    }
    case EIGEN_EV_KEY: {
        eui_event e; memset(&e, 0, sizeof(e));
        e.type = EUI_EV_KEYDOWN; e.x = ev->a; e.y = ev->b;
        e.key = (uint16_t)ev->a; e.code = (uint8_t)ev->b;
        if (w->focused) eui_widget_event(w->focused, &e, w);
        w->dirty = 1;
        break;
    }
    case EIGEN_EV_CLOSE:
        w->running = 0;
        break;
    default: break;
    }
}

static void eui_window_redraw(eui_window* w) {
    eui_canvas* c = &w->canvas;
    int lw = (int)(c->w / w->scale) + 1;
    int lh = (int)(c->h / w->scale) + 1;
    eui_canvas_fill_rect(c, 0, 0, lw, lh, w->theme->bg);
    w->root->x = 0; w->root->y = 0; w->root->w = lw; w->root->h = lh;
    eui_widget_draw(w->root, c, w->theme);
    w->dirty = 0;
}

void eui_window_step(eui_window* w) {
    eigen_ev_t evs[16];
    int n = eigen_win_poll(w->id, evs, 16);
    for (int i = 0; i < n; i++) eui_window_handle(w, &evs[i]);
    int anims = eui_anim_tick_all(16.0f);
    if (w->dirty || anims) {
        eui_window_redraw(w);
        eigen_win_flush(w->id);
    }
}

void eui_window_run(eui_window* w) {
    while (w->running) {
        eui_window_step(w);
        eigen_sleep_ms(16);
    }
    eui_window_free(w);
}
