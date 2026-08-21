/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* userui.c — ring-3 UI toolkit implementation.
 *
 * All widgets draw into the caller's window buffer and animate from a
 * tiny per-rect state table (hover fade ~300ms, press tint). The app
 * never manages widget state — it only reads the return values.
 */
#include "userui.h"
#include "userlib.h"
#include <user/eigen.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Theme                                                              */
/* ------------------------------------------------------------------ */
ui_theme_t ui_theme = {
    0x0B1020,  /* bg        deep navy          */
    0x141C30,  /* panel                        */
    0x1C2742,  /* panel2    raised             */
    0x2A3A5C,  /* border                       */
    0x3D5282,  /* border2   bright             */
    0xE6EDF3,  /* text                         */
    0x8A94A6,  /* dim                          */
    0x56617A,  /* faint                        */
    0x3B82F6,  /* accent    blue               */
    0x38BDF8,  /* accent2   sky                */
    0x22C55E,  /* good                         */
    0xEF4444,  /* bad                          */
    0xFACC15   /* gold                         */
};

/* ------------------------------------------------------------------ */
/* Colour math                                                        */
/* ------------------------------------------------------------------ */
uint32_t ui_lerp_rgb(uint32_t a, uint32_t b, int t, int T) {
    if (T <= 0) return b;
    if (t <= 0) return a;
    if (t >= T) return b;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * t / T;
    int g = ag + (bg - ag) * t / T;
    int bl = ab + (bb - ab) * t / T;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

uint32_t ui_lighten(uint32_t c, int amt) {
    if (amt > 100) amt = 100; if (amt < -100) amt = -100;
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    if (amt >= 0) {
        r += (255 - r) * amt / 100; g += (255 - g) * amt / 100; b += (255 - b) * amt / 100;
    } else {
        int d = -amt;
        r = r * (100 - d) / 100; g = g * (100 - d) / 100; b = b * (100 - d) / 100;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t ui_darken(uint32_t c, int amt) { return ui_lighten(c, -amt); }

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

/* alpha blend one pixel: dst = (src*a + dst*(255-a)) / 255 */
static void blend_px(uint32_t* buf, int W, int H, int x, int y, uint32_t rgb, int a) {
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
    if (a >= 255) { buf[y * W + x] = rgb; return; }
    uint32_t old = buf[y * W + x];
    int sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    int dr = (old >> 16) & 0xFF, dg = (old >> 8) & 0xFF, db = old & 0xFF;
    int r = (sr * a + dr * (255 - a)) / 255;
    int g = (sg * a + dg * (255 - a)) / 255;
    int b = (sb * a + db * (255 - a)) / 255;
    buf[y * W + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void ui_fill_round(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, int r, uint32_t rgb) {
    if (rw <= 0 || rh <= 0) return;
    if (r > rw / 2) r = rw / 2;
    if (r > rh / 2) r = rh / 2;
    if (r < 1) { eigen_draw_fillrect(buf, w, h, x, y, rw, rh, rgb); return; }
    /* Per-pixel rounded-rect: a pixel is inside if it lies within radius r of
       a corner centre; outside the corner bands every pixel is inside. */
    int r2 = r * r;
    for (int yy = 0; yy < rh; yy++) {
        int py = y + yy;
        if (py < 0 || py >= h) continue;
        int dy = (yy < r) ? r - yy : (yy >= rh - r ? yy - (rh - r - 1) : 0);
        int dy2 = dy * dy;
        for (int xx = 0; xx < rw; xx++) {
            int px = x + xx;
            if (px < 0 || px >= w) continue;
            int dx = (xx < r) ? r - xx : (xx >= rw - r ? xx - (rw - r - 1) : 0);
            if (dx * dx + dy2 <= r2)
                buf[py * w + px] = rgb;
        }
    }
}

void ui_draw_round(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, int r, uint32_t rgb) {
    if (rw <= 0 || rh <= 0) return;
    if (r > rw / 2) r = rw / 2;
    if (r > rh / 2) r = rh / 2;
    if (r < 1) { eigen_draw_rect(buf, w, h, x, y, rw, rh, rgb); return; }
    /* Same inside test as ui_fill_round; a pixel is on the 1px outline when
       at least one of its four neighbours falls outside the shape. */
    int r2 = r * r;
    for (int yy = 0; yy < rh; yy++) {
        int py = y + yy;
        if (py < 0 || py >= h) continue;
        int dy = (yy < r) ? r - yy : (yy >= rh - r ? yy - (rh - r - 1) : 0);
        int dy2 = dy * dy;
        for (int xx = 0; xx < rw; xx++) {
            int px = x + xx;
            if (px < 0 || px >= w) continue;
            int dx = (xx < r) ? r - xx : (xx >= rw - r ? xx - (rw - r - 1) : 0);
            if (dx * dx + dy2 > r2) continue;          /* outside the shape */
            int out = (xx == 0 || yy == 0 || xx == rw - 1 || yy == rh - 1);
            if (!out) {
                int dxl = (xx-1 < r) ? r-(xx-1) : (xx-1 >= rw-r ? (xx-1)-(rw-r-1) : 0);
                if (dxl*dxl + dy2 > r2) out = 1;
            }
            if (!out) {
                int dxr = (xx+1 < r) ? r-(xx+1) : (xx+1 >= rw-r ? (xx+1)-(rw-r-1) : 0);
                if (dxr*dxr + dy2 > r2) out = 1;
            }
            if (!out) {
                int dyu = (yy-1 < r) ? r-(yy-1) : (yy-1 >= rh-r ? (yy-1)-(rh-r-1) : 0);
                if (dx*dx + dyu*dyu > r2) out = 1;
            }
            if (!out) {
                int dyd = (yy+1 < r) ? r-(yy+1) : (yy+1 >= rh-r ? (yy+1)-(rh-r-1) : 0);
                if (dx*dx + dyd*dyd > r2) out = 1;
            }
            if (out) buf[py * w + px] = rgb;
        }
    }
}

void ui_vgrad(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t top, uint32_t bot) {
    if (rh <= 0) return;
    for (int i = 0; i < rh; i++) {
        uint32_t c = ui_lerp_rgb(top, bot, i, rh - 1);
        eigen_draw_fillrect(buf, w, h, x, y + i, rw, 1, c);
    }
}

void ui_glow(uint32_t* buf, int w, int h, int cx, int cy, int r, uint32_t rgb, int peak) {
    if (r <= 0 || peak <= 0) return;
    int steps = r / 2;
    if (steps < 1) steps = 1;
    for (int i = 0; i < steps; i++) {
        int rr = r - i * 2;                   /* shrink toward centre   */
        int a = peak * (steps - i) / steps;   /* stronger at centre     */
        if (rr <= 0) rr = 1;
        int rr2 = rr * rr;
        int in2 = (rr - 2) * (rr - 2);
        if (in2 < 0) in2 = 0;
        for (int py = -rr; py <= rr; py++) {
            for (int px = -rr; px <= rr; px++) {
                int d = px * px + py * py;
                if (d <= rr2 && (rr < 3 || d > in2))
                    blend_px(buf, w, h, cx + px, cy + py, rgb, a);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Theme sync — pull the LIVE OS palette so ring-3 apps match the shell */
/* instead of a hard-coded off-brand scheme. Call once at startup.      */
/* ------------------------------------------------------------------ */
void ui_sync_theme(void) {
    uint32_t pal[EIGEN_THEME_COUNT];
    int n = eigen_win_gettheme(pal, EIGEN_THEME_COUNT);
    if (n <= 0) return;   /* keep the modern factory default if syscall fails */
    uint32_t bg     = pal[EIGEN_THEME_WINTITLE]; /* window bg   */
    uint32_t surf   = pal[EIGEN_THEME_SURFACE];   /* raised      */
    uint32_t sv     = pal[EIGEN_THEME_SURFACE_VAR];
    uint32_t outline= pal[EIGEN_THEME_OUTLINE];
    uint32_t accent = pal[EIGEN_THEME_ACCENT];
    uint32_t text   = pal[EIGEN_THEME_PRIMARY];
    uint32_t dim    = pal[EIGEN_THEME_SECONDARY];
    uint32_t faint  = pal[EIGEN_THEME_DISABLED];
    uint32_t err    = pal[EIGEN_THEME_ERROR];
    uint32_t winb   = pal[EIGEN_THEME_WINBORDER];

    ui_theme.bg      = bg;
    ui_theme.panel   = sv;
    ui_theme.panel2  = surf;
    ui_theme.border  = outline;
    ui_theme.border2 = winb;
    ui_theme.text    = text;
    ui_theme.dim     = dim;
    ui_theme.faint   = faint;
    ui_theme.accent  = accent;
    ui_theme.accent2 = ui_lighten(accent, 40);
    ui_theme.good    = ui_lerp_rgb(accent, 0x22C55E, 60, 100); /* blend with green */
    ui_theme.bad     = err;
    ui_theme.gold    = ui_lighten(accent, 20);
}

/* ------------------------------------------------------------------ */
/* Context                                                            */
/* ------------------------------------------------------------------ */
static void anim_decay(void);   /* fwd: fades hover of undrawn widgets */

void ui_begin(ui_t* u, uint32_t* buf, int W, int H) {
    u->buf = buf; u->W = W; u->H = H;
    u->click = 0;
    u->last_key = 0;
    if (u->focus < -1) u->focus = -1;
    anim_decay();                     /* fade hover of widgets not drawn */
}

void ui_feed(ui_t* u, const eigen_ev_t* evs, int n) {
    for (int i = 0; i < n; i++) {
        switch (evs[i].type) {
        case EIGEN_EV_MMOVE: u->mx = evs[i].a; u->my = evs[i].b; break;
        case EIGEN_EV_MDOWN: if (evs[i].c == 1) u->down = 1; break;
        case EIGEN_EV_MUP:   if (evs[i].c == 1) { u->down = 0; u->click = 1; } break;
        case EIGEN_EV_KEY:   u->last_key = evs[i].a; break;
        default: break;
        }
    }
}

void ui_end(ui_t* u) {
    u->click = 0;
    u->last_key = 0;
}

/* ------------------------------------------------------------------ */
/* Hover animation slots (auto keyed by rect, decay when undrawn)     */
/* ------------------------------------------------------------------ */
#define ANIM_SLOTS 24
typedef struct { int x, y, w, h; int t; } anim_t;
static anim_t g_anims[ANIM_SLOTS];

static int anim_touch(int x, int y, int w, int h, int up) {
    /* find a slot for this rect */
    int best = -1;
    for (int i = 0; i < ANIM_SLOTS; i++) {
        if (g_anims[i].w > 0 && g_anims[i].x == x && g_anims[i].y == y &&
            g_anims[i].w == w && g_anims[i].h == h) { best = i; break; }
        if (best < 0 && g_anims[i].w == 0) best = i;
    }
    if (best < 0) return 0;
    anim_t* a = &g_anims[best];
    if (a->w == 0) { a->x = x; a->y = y; a->w = w; a->h = h; a->t = 0; }
    if (up) { if (a->t < 255) a->t += 90; if (a->t > 255) a->t = 255; }
    else    { if (a->t > 0)   a->t -= 90; if (a->t < 0)   a->t = 0; }
    return a->t;
}

static void anim_decay(void) {
    for (int i = 0; i < ANIM_SLOTS; i++) {
        if (g_anims[i].w > 0) {
            if (g_anims[i].t > 0) g_anims[i].t -= 45;
            if (g_anims[i].t <= 0) { g_anims[i].w = 0; }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Widgets                                                            */
/* ------------------------------------------------------------------ */
void ui_panel(ui_t* u, int x, int y, int w, int h) {
    eigen_draw_fillrect(u->buf, u->W, u->H, x, y, w, h, ui_theme.panel);
    eigen_draw_rect(u->buf, u->W, u->H, x, y, w, h, ui_theme.border);
}

void ui_card(ui_t* u, int x, int y, int w, int h) {
    ui_vgrad(u->buf, u->W, u->H, x, y, w, h, ui_theme.panel2, ui_theme.panel);
    eigen_draw_rect(u->buf, u->W, u->H, x, y, w, h, ui_theme.border);
    eigen_draw_fillrect(u->buf, u->W, u->H, x + 1, y + 1, w - 2, 1, ui_theme.border2);
}

void ui_label(ui_t* u, int x, int y, const char* s, uint32_t col) {
    eigen_draw_text(u->buf, u->W, u->H, x, y, s, col);
}

void ui_header(ui_t* u, const char* title, const char* sub) {
    eigen_draw_fillrect(u->buf, u->W, u->H, 0, 0, u->W, 30, ui_theme.bg);
    eigen_draw_fillrect(u->buf, u->W, u->H, 0, 0, 3, 30, ui_theme.accent);
    ui_glow(u->buf, u->W, u->H, 2, 15, 22, ui_theme.accent2, 60);
    eigen_draw_text(u->buf, u->W, u->H, 14, 7, title, ui_theme.text);
    if (sub)
        eigen_draw_text(u->buf, u->W, u->H, 14 + 8 * 12, 7, sub, ui_theme.dim);
}

void ui_icon(ui_t* u, int x, int y, int sz, vector_icon_id_t icon_id, uint32_t col) {
    draw_vector_icon(u->buf, u->W, u->H, x, y, sz, icon_id, col);
}

int ui_button_col(ui_t* u, int x, int y, int w, int h, const char* label, uint32_t col) {
    int hov = u->mx >= x && u->mx < x + w && u->my >= y && u->my < y + h;
    int t = anim_touch(x, y, w, h, hov);
    int pressed = hov && u->down;

    uint32_t fill = ui_theme.panel;
    if (pressed) fill = ui_darken(col, 45);
    else if (t > 0) fill = ui_lerp_rgb(ui_theme.panel, ui_darken(col, 25), t, 255);

    ui_fill_round(u->buf, u->W, u->H, x, y, w, h, 7, fill);
    if (hov && !pressed) ui_glow(u->buf, u->W, u->H, x + w / 2, y + h / 2, w / 2, col, 28);
    ui_draw_round(u->buf, u->W, u->H, x, y, w, h, 7, hov ? ui_theme.border2 : ui_theme.border);

    uint32_t tc = pressed ? ui_theme.text : (hov ? ui_theme.text : ui_lighten(col, 40));
    int tw = 0; while (label[tw]) tw++;
    eigen_draw_text(u->buf, u->W, u->H, x + (w - tw * 8) / 2, y + (h - 16) / 2, label, tc);
    return hov && u->click;
}

int ui_button(ui_t* u, int x, int y, int w, int h, const char* label) {
    return ui_button_col(u, x, y, w, h, label, ui_theme.accent);
}

int ui_button_icon(ui_t* u, int x, int y, int w, int h, vector_icon_id_t icon_id, const char* label, uint32_t col) {
    int hov = u->mx >= x && u->mx < x + w && u->my >= y && u->my < y + h;
    int t = anim_touch(x, y, w, h, hov);
    int pressed = hov && u->down;

    uint32_t fill = ui_theme.panel;
    if (pressed) fill = ui_darken(col, 45);
    else if (t > 0) fill = ui_lerp_rgb(ui_theme.panel, ui_darken(col, 25), t, 255);

    ui_fill_round(u->buf, u->W, u->H, x, y, w, h, 7, fill);
    if (hov && !pressed) ui_glow(u->buf, u->W, u->H, x + w / 2, y + h / 2, w / 2, col, 28);
    ui_draw_round(u->buf, u->W, u->H, x, y, w, h, 7, hov ? ui_theme.border2 : ui_theme.border);

    uint32_t tc = pressed ? ui_theme.text : (hov ? ui_theme.text : ui_lighten(col, 40));
    int isz = 18;
    int tw = label ? (int)strlen(label) * 8 : 0;
    int total_w = isz + (tw > 0 ? 6 + tw : 0);
    int start_x = x + (w - total_w) / 2;
    draw_vector_icon(u->buf, u->W, u->H, start_x, y + (h - isz) / 2, isz, icon_id, tc);
    if (label && *label) {
        eigen_draw_text(u->buf, u->W, u->H, start_x + isz + 6, y + (h - 16) / 2, label, tc);
    }
    return hov && u->click;
}

int ui_toggle(ui_t* u, int x, int y, int* on) {
    int w = 44, h = 22;
    int hov = u->mx >= x && u->mx < x + w && u->my >= y && u->my < y + h;
    int t = anim_touch(x, y, w, h, hov || *on);
    if (hov && u->click) *on = !*on;

    uint32_t track = *on ? ui_theme.accent : ui_theme.panel2;
    if (t > 0 && !*on) track = ui_lerp_rgb(ui_theme.panel2, ui_theme.accent, t, 255);
    ui_fill_round(u->buf, u->W, u->H, x, y, w, h, 11, track);
    ui_draw_round(u->buf, u->W, u->H, x, y, w, h, 11, ui_theme.border);
    if (*on) ui_glow(u->buf, u->W, u->H, x + w / 2, y + h / 2, 20, ui_theme.accent2, 40);
    int knob = *on ? x + w - 20 : x + 2;
    eigen_draw_fillcircle(u->buf, u->W, u->H, knob + 9, y + h / 2, 8, 0xF4F7FB);
    return 0;
}

int ui_slider(ui_t* u, int x, int y, int w, int* val, int vmin, int vmax) {
    int h = 16;
    int hov = u->mx >= x - 8 && u->mx < x + w + 8 && u->my >= y && u->my < y + h;
    int grabbed = 0;
    if (hov && u->down) {
        int t = u->mx - x;
        if (t < 0) t = 0; if (t > w) t = w;
        *val = vmin + t * (vmax - vmin) / w;
        grabbed = 1;
    }
    if (hov && u->click) grabbed = 1;

    eigen_draw_fillrect(u->buf, u->W, u->H, x, y + h / 2 - 2, w, 4, ui_theme.panel2);
    int t = (*val - vmin) * w / (vmax - vmin);
    if (t > w) t = w; if (t < 0) t = 0;
    if (t > 0) eigen_draw_fillrect(u->buf, u->W, u->H, x, y + h / 2 - 2, t, 4, ui_theme.accent);
    int knob = x + t;
    eigen_draw_fillcircle(u->buf, u->W, u->H, knob, y + h / 2, 7, hov ? 0xFFFFFF : ui_theme.accent2);
    if (hov) ui_glow(u->buf, u->W, u->H, knob, y + h / 2, 14, ui_theme.accent2, 50);
    return grabbed;
}

int ui_text_input(ui_t* u, int id, int x, int y, int w, int h,
                  char* buf, int maxlen) {
    int hov = u->mx >= x && u->mx < x + w && u->my >= y && u->my < y + h;
    if (hov && u->click) u->focus = id;
    int focused = (u->focus == id);

    uint32_t fill = focused ? ui_theme.panel2 : ui_theme.panel;
    ui_fill_round(u->buf, u->W, u->H, x, y, w, h, 7, fill);
    ui_draw_round(u->buf, u->W, u->H, x, y, w, h, 7,
                  focused ? ui_theme.accent : ui_theme.border);
    if (focused) ui_glow(u->buf, u->W, u->H, x + w / 2, y + h / 2, 16, ui_theme.accent, 40);

    int len = 0; while (len < maxlen && buf[len]) len++;
    int changed = 0, commit = 0;
    if (focused) {
        int k = u->last_key;
        if (k == 13 || k == '\n') { commit = 1; u->focus = -1; }  /* Enter */
        else if (k == 27) { u->focus = -1; }
        else if (k == 8) { if (len > 0) { buf[--len] = 0; changed = 1; } }
        else if (k >= 32 && k < 127 && len < maxlen - 1) {
            buf[len++] = (char)k; buf[len] = 0; changed = 1;
        }
    }
    eigen_draw_text(u->buf, u->W, u->H, x + 8, y + (h - 16) / 2, buf,
                    len ? ui_theme.text : ui_theme.faint);
    if (focused) {
        unsigned long now = (unsigned long)eigen_gettime_ms();
        if ((now / 400) & 1) {
            int cx = x + 8 + len * 8;
            eigen_draw_fillrect(u->buf, u->W, u->H, cx, y + 3, 2, h - 6, ui_theme.accent2);
        }
    }
    return commit;
}

static int hov_check(ui_t* u, int x, int y, int w, int h) {
    return u->mx >= x && u->mx < x + w && u->my >= y && u->my < y + h;
}

int ui_list(ui_t* u, int id, int x, int y, int w, int h,
            char rows[][48], int nrows, int* sel, int row_h,
            const uint32_t* row_colors) {
    (void)id;
    if (hov_check(u, x, y, w, h)) {
        int k = u->last_key;
        if (k == 128 && *sel > 0) *sel -= 1;        /* wheel up    */
        if (k == 129 && *sel < nrows - 1) *sel += 1;/* wheel down  */
    }
    int changed = 0;
    int vis = h / row_h;
    if (vis > nrows) vis = nrows;
    int top = *sel - vis / 2;                       /* keep selection visible */
    if (top < 0) top = 0;
    if (top > nrows - vis) top = nrows - vis;
    if (top < 0) top = 0;

    for (int r = 0; r < vis; r++) {
        int ry = y + 2 + r * row_h;
        int rr = top + r;
        int is_sel = (rr == *sel);
        int hov = hov_check(u, x + 2, ry - 2, w - 4, row_h - 2);
        if (is_sel) {
            ui_fill_round(u->buf, u->W, u->H, x + 2, ry - 1, w - 4, row_h - 2, 6,
                          ui_theme.panel2);
            ui_draw_round(u->buf, u->W, u->H, x + 2, ry - 1, w - 4, row_h - 2, 6,
                          ui_theme.border2);
        } else if (hov) {
            ui_fill_round(u->buf, u->W, u->H, x + 2, ry - 1, w - 4, row_h - 2, 6,
                          ui_theme.panel2);
        }
        uint32_t rcol = is_sel ? ui_theme.text : ui_theme.dim;
        if (row_colors) rcol = row_colors[rr];
        eigen_draw_text(u->buf, u->W, u->H, x + 10, ry, rows[rr], rcol);
        if (hov && u->click && rr != *sel) { *sel = rr; changed = 1; }
    }
    return changed;
}