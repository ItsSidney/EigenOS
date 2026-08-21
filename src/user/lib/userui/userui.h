/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* userui.h — ring-3 UI toolkit (draws into your window buffer).
 *
 * A tiny immediate-mode widget set so ring-3 apps look modern with
 * almost no code. The pattern per frame:
 *
 *     ui_t u;
 *     ui_begin(&u, buf, W, H);          // once per frame
 *     ui_feed(&u, evs, got);            // hand it the polled events
 *     ... draw anything, then:
 *     if (ui_button(&u, 10, 40, 120, 26, "Save")) save();   // clicked?
 *     if (ui_text_input(&u, 1, 10, 90, 200, 24, name, 32)) do_commit();
 *     ui_end(&u);                       // clears click latch
 *
 * Everything is drawn by the widget; the app only reads returns.
 * Hover/press animations, glows and the caret blink are automatic.
 */
#ifndef USERUI_H
#define USERUI_H

#include <stdint.h>
#include "userlib.h"

/* ------------------------------------------------------------------ */
/* Theme                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t bg;        /* window background             */
    uint32_t panel;     /* card / panel fill             */
    uint32_t panel2;    /* raised / hover fill           */
    uint32_t border;    /* card outline                  */
    uint32_t border2;   /* bright outline                */
    uint32_t text;      /* primary text                  */
    uint32_t dim;       /* secondary text                */
    uint32_t faint;     /* disabled / hint text          */
    uint32_t accent;    /* primary accent                */
    uint32_t accent2;   /* accent highlight              */
    uint32_t good;      /* success                       */
    uint32_t bad;       /* danger                        */
    uint32_t gold;      /* highlight / best              */
} ui_theme_t;

extern ui_theme_t ui_theme;   /* the house dark theme (customise away) */

/* ------------------------------------------------------------------ */
/* Colour + drawing helpers (beyond eigen_draw_*)                     */
/* ------------------------------------------------------------------ */
uint32_t ui_lerp_rgb(uint32_t a, uint32_t b, int t, int T);   /* t in 0..T */
uint32_t ui_lighten(uint32_t c, int amt);   /* amt -100..100 */
uint32_t ui_darken(uint32_t c, int amt);

void ui_fill_round(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, int r, uint32_t rgb);
void ui_draw_round(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, int r, uint32_t rgb);
void ui_vgrad(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t top, uint32_t bot);
void ui_glow(uint32_t* buf, int w, int h, int cx, int cy, int r, uint32_t rgb, int peak);

/* ------------------------------------------------------------------ */
/* Context — feed it events, read the widgets                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t* buf;
    int W, H;           /* content buffer size          */
    int mx, my;         /* mouse (content-relative)     */
    int down;           /* left button held             */
    int click;          /* left released this frame     */
    int focus;          /* focused widget id (-1 none)  */
    int last_key;       /* key code from EIGEN_EV_KEY   */
} ui_t;

void ui_begin(ui_t* u, uint32_t* buf, int W, int H);
void ui_feed(ui_t* u, const eigen_ev_t* evs, int n);
void ui_end(ui_t* u);
/* Pull the LIVE OS theme palette into ui_theme so ring-3 apps match the
   shell's look. Call once at startup (after win create, before first draw). */
void ui_sync_theme(void);

#include "vector_icons.h"

/* ------------------------------------------------------------------ */
/* Widgets — all return their interaction edge (1 = happened)         */
/* ------------------------------------------------------------------ */
void ui_panel(ui_t* u, int x, int y, int w, int h);          /* flat card */
void ui_card(ui_t* u, int x, int y, int w, int h);           /* card + glow top edge */
void ui_label(ui_t* u, int x, int y, const char* s, uint32_t col);
void ui_header(ui_t* u, const char* title, const char* sub); /* accent bar + glow */
void ui_icon(ui_t* u, int x, int y, int sz, vector_icon_id_t icon_id, uint32_t col);
int  ui_button(ui_t* u, int x, int y, int w, int h, const char* label);
int  ui_button_col(ui_t* u, int x, int y, int w, int h, const char* label, uint32_t col);
int  ui_button_icon(ui_t* u, int x, int y, int w, int h, vector_icon_id_t icon_id, const char* label, uint32_t col);
int  ui_toggle(ui_t* u, int x, int y, int* on);              /* animated switch */
int  ui_slider(ui_t* u, int x, int y, int w, int* val, int vmin, int vmax);
int  ui_text_input(ui_t* u, int id, int x, int y, int w, int h,
                   char* buf, int maxlen);                   /* 1 on Enter */
int  ui_list(ui_t* u, int id, int x, int y, int w, int h,
             char rows[][48], int nrows, int* sel, int row_h,
             const uint32_t* row_colors); /* per-row colour or NULL */

#endif /* USERUI_H */