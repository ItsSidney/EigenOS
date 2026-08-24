/***************************************************************/
/*  EigenUI Widgets — implementation                            */
/***************************************************************/
#include "eui_private.h"
#include "eui_widgets.h"
#include "eui_font.h"
#include "eui_anim.h"
#include "eui_window.h"
#include "userlib.h"
#include <stdlib.h>
#include <string.h>

/* ── shared ──────────────────────────────────────────────────── */
typedef struct { char text[64]; eui_color color; } list_item_t;

static int measure_text_w(const char* s, int size) {
    eui_canvas c; c.buf = 0; c.w = 0; c.h = 0; c.scale = 1.0f; c.clip_sp = 0;
    int w = 0, h = 0; eui_font_measure(&c, s, size, &w, &h); return w;
}
static void emit_change(eui_widget* w) { if (w->on_change) w->on_change(w, w->on_change_ud); }

/* ── Label ───────────────────────────────────────────────────── */
typedef struct { char text[256]; eui_color color; int align; } label_data;
eui_widget* eui_label_new(const char* text) {
    label_data* d = (label_data*)eigen_malloc(sizeof(label_data));
    memset(d, 0, sizeof(*d));
    strncpy(d->text, text ? text : "", 255); d->text[255] = 0;
    d->color = EUI_NONE; d->align = 0;
    eui_widget* w = eui_widget_alloc(EUI_KIND_LABEL, d);
    w->pref_w = measure_text_w(d->text, 14) + 4;
    w->pref_h = 18;
    return w;
}
void eui_label_set_text(eui_widget* w, const char* t) {
    if (!w || w->kind != EUI_KIND_LABEL) return;
    strncpy(((label_data*)w->data)->text, t ? t : "", 255);
    ((label_data*)w->data)->text[255] = 0; w->dirty = 1;
}
void eui_label_set_color(eui_widget* w, eui_color c) { if (w) ((label_data*)w->data)->color = c; }
void eui_label_set_align(eui_widget* w, int a) { if (w) ((label_data*)w->data)->align = a; }

void eui_label_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_label_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    label_data* d = (label_data*)w->data;
    eui_color col = (d->color == EUI_NONE) ? t->text : d->color;
    int tw = measure_text_w(d->text, t->font_size);
    int tx = w->x;
    if (d->align == 1) tx = w->x + (w->w - tw) / 2;
    else if (d->align == 2) tx = w->x + w->w - tw;
    eui_font_draw(c, tx, w->y + (w->h - t->font_size) / 2, t->font_size, col, d->text);
}
int eui_label_event(eui_widget* w, const eui_event* e, struct eui_window* win) { (void)w;(void)e;(void)win; return 0; }

/* ── Button ──────────────────────────────────────────────────── */
typedef struct { char label[128]; eui_icon_id icon; int pressed; int hovered; float press; eui_anim* anim; } button_data;
static void btn_anim_cb(float v, void* ud) { ((button_data*)ud)->press = v; }
eui_widget* eui_button_new(const char* label) {
    button_data* d = (button_data*)eigen_malloc(sizeof(button_data));
    memset(d, 0, sizeof(*d));
    strncpy(d->label, label ? label : "", 127); d->label[127] = 0;
    d->icon = EUI_ICON_NONE; d->press = 0;
    d->anim = eui_anim_new(0, 0, 120, btn_anim_cb, d);
    eui_widget* w = eui_widget_alloc(EUI_KIND_BUTTON, d);
    w->pref_h = 26; w->pref_w = measure_text_w(d->label, 14) + 28;
    return w;
}
void eui_button_set_icon(eui_widget* w, eui_icon_id id) {
    if (!w || w->kind != EUI_KIND_BUTTON) return;
    ((button_data*)w->data)->icon = id; w->pref_w = measure_text_w(((button_data*)w->data)->label, 14) + 48; w->dirty = 1;
}
void eui_button_set_label(eui_widget* w, const char* l) { if (w) eui_label_set_text(w, l); }

void eui_button_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_button_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    button_data* d = (button_data*)w->data;
    int off = (int)(d->press * 1.5f);
    eui_color base = w->enabled ? t->accent : t->surface_var;
    eui_color top = eui_lighten(base, 14 - (int)(d->press * 18));
    eui_color bot = eui_darken(base, 6 + (int)(d->press * 10));
    if (!w->enabled) { top = t->surface_var; bot = t->surface_var; }
    if (!d->pressed && w->hovered && w->enabled) top = eui_lighten(top, 8);
    if (!w->enabled) { eui_canvas_round(c, w->x, w->y + off, w->w, w->h, t->radius, t->surface_var); }
    else {
        eui_canvas_shadow(c, w->x, w->y + off + t->shadow_off, w->w, w->h, t->radius, t->shadow_blur, t->shadow);
        eui_canvas_vgrad(c, w->x, w->y + off, w->w, w->h, t->radius, top, bot);
        eui_canvas_round_stroke(c, w->x, w->y + off, w->w, w->h, t->radius, 1, eui_lighten(top, 22));
    }
    int cx = w->x + 10;
    if (d->icon != EUI_ICON_NONE) {
        eui_icon_draw(c, d->icon, cx, w->y + (w->h - 16) / 2 + off, 16, w->enabled ? t->on_primary : t->dim);
        cx += 22;
    }
    eui_color tc = w->enabled ? t->on_primary : t->dim;
    eui_font_draw(c, cx, w->y + (w->h - t->font_size) / 2 + off, t->font_size, tc, d->label);
}
int eui_button_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    button_data* d = (button_data*)w->data;
    if (!w->enabled) return 0;
    if (e->type == EUI_EV_MOUSEDOWN && EUI_IN_RECT(e->x, e->y, w->x, w->y, w->w, w->h)) {
        d->pressed = 1; eui_anim_set(d->anim, 1.0f); return 1;
    }
    if (e->type == EUI_EV_MOUSEUP && d->pressed) {
        d->pressed = 0; eui_anim_set(d->anim, 0.0f);
        if (w->on_click) w->on_click(w, w->on_click_ud);
        return 1;
    }
    if (e->type == EUI_EV_MOUSEMOVE) { d->hovered = w->hovered; }
    return 0;
}

/* ── Toggle (switch / checkbox) ─────────────────────────────── */
typedef struct { int on; int style; char text[128]; int hovered; float tg; eui_anim* anim; } toggle_data;
static void tg_anim_cb(float v, void* ud) { ((toggle_data*)ud)->tg = v; }
eui_widget* eui_toggle_new(int style, const char* text) {
    toggle_data* d = (toggle_data*)eigen_malloc(sizeof(toggle_data));
    memset(d, 0, sizeof(*d));
    d->on = 0; d->style = style; d->tg = 0;
    strncpy(d->text, text ? text : "", 127); d->text[127] = 0;
    d->anim = eui_anim_new(0, 0, 140, tg_anim_cb, d);
    eui_widget* w = eui_widget_alloc(EUI_KIND_TOGGLE, d);
    w->pref_h = 24;
    w->pref_w = (style == 1) ? 32 + 8 + measure_text_w(d->text, 14) : 52;
    return w;
}
int eui_toggle_get(eui_widget* w) { return w && w->kind == EUI_KIND_TOGGLE ? ((toggle_data*)w->data)->on : 0; }
void eui_toggle_set(eui_widget* w, int on) {
    if (!w || w->kind != EUI_KIND_TOGGLE) return;
    toggle_data* d = (toggle_data*)w->data;
    if (d->on != on) { d->on = on; eui_anim_set(d->anim, on ? 1.0f : 0.0f); w->dirty = 1; }
}
void eui_toggle_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_toggle_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    toggle_data* d = (toggle_data*)w->data;
    int cy = w->y + w->h / 2;
    if (d->style == 0) { /* switch */
        int tw = 42, th = 22, sx = w->x, sy = cy - th / 2;
        eui_color tr = d->on ? t->accent : t->surface_var;
        eui_canvas_round(c, sx, sy, tw, th, th / 2, tr);
        eui_canvas_round_stroke(c, sx, sy, tw, th, th / 2, 1, eui_darken(tr, 12));
        int kx = sx + 2 + (int)(d->tg * (tw - 4 - 18));
        eui_canvas_round(c, kx, sy + 2, 18, th - 4, (th - 4) / 2, t->surface);
    } else { /* checkbox */
        int bs = 18, sx = w->x, sy = cy - bs / 2;
        eui_canvas_round(c, sx, sy, bs, bs, 4, d->on ? t->accent : t->surface);
        eui_canvas_round_stroke(c, sx, sy, bs, bs, 4, 1, t->border);
        if (d->on) eui_icon_draw(c, EUI_ICON_CHECK, sx, sy, bs, t->on_primary);
        if (d->text[0]) eui_font_draw(c, sx + bs + 8, cy - t->font_size / 2, t->font_size, t->text, d->text);
    }
}
int eui_toggle_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    toggle_data* d = (toggle_data*)w->data;
    if (e->type == EUI_EV_MOUSEDOWN && EUI_IN_RECT(e->x, e->y, w->x, w->y, w->w, w->h)) {
        d->on = !d->on; eui_anim_set(d->anim, d->on ? 1.0f : 0.0f);
        emit_change(w); return 1;
    }
    return 0;
}

/* ── Slider ──────────────────────────────────────────────────── */
typedef struct { int value, min, max; int hovered; float knob; eui_anim* anim; int dragging; } slider_data;
static void sl_anim_cb(float v, void* ud) { ((slider_data*)ud)->knob = v; }
eui_widget* eui_slider_new(int min, int max, int value) {
    slider_data* d = (slider_data*)eigen_malloc(sizeof(slider_data));
    memset(d, 0, sizeof(*d));
    d->min = min; d->max = max > min ? max : min + 1; d->value = value;
    d->knob = (float)(value - min) / (d->max - min);
    d->anim = eui_anim_new(0, 0, 90, sl_anim_cb, d);
    eui_widget* w = eui_widget_alloc(EUI_KIND_SLIDER, d);
    w->pref_h = 24; w->pref_w = 160; w->expand_x = 1; w->weight_x = 1;
    return w;
}
int eui_slider_get(eui_widget* w) { return w && w->kind == EUI_KIND_SLIDER ? ((slider_data*)w->data)->value : 0; }
void eui_slider_set(eui_widget* w, int v) {
    if (!w || w->kind != EUI_KIND_SLIDER) return;
    slider_data* d = (slider_data*)w->data;
    d->value = eui_clamp_i(v, d->min, d->max);
    eui_anim_set(d->anim, (float)(d->value - d->min) / (d->max - d->min));
    w->dirty = 1;
}
void eui_slider_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_slider_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    slider_data* d = (slider_data*)w->data;
    int pad = 8, track_h = 6, ty = w->y + w->h / 2 - track_h / 2;
    int tx = w->x + pad, tw = w->w - 2 * pad;
    eui_canvas_round(c, tx, ty, tw, track_h, track_h / 2, t->surface_var);
    int fw = (int)(tw * d->knob);
    eui_canvas_round(c, tx, ty, fw, track_h, track_h / 2, t->accent);
    int kx = tx + (int)(tw * d->knob), ky = w->y + w->h / 2;
    eui_canvas_round(c, kx - 8, ky - 8, 16, 16, 8, t->surface);
    eui_canvas_round_stroke(c, kx - 8, ky - 8, 16, 16, 8, 1, t->border);
}
static void slider_set_from_x(slider_data* d, int x, int wx, int ww) {
    int pad = 8, tx = wx + pad, tw = ww - 2 * pad;
    float frac = (float)(x - tx) / tw; if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    int v = d->min + (int)(frac * (d->max - d->min) + 0.5f);
    if (v != d->value) { d->value = v; eui_anim_set(d->anim, frac); }
}
int eui_slider_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    slider_data* d = (slider_data*)w->data;
    if (e->type == EUI_EV_MOUSEDOWN && EUI_IN_RECT(e->x, e->y, w->x, w->y, w->w, w->h)) {
        d->dragging = 1; slider_set_from_x(d, e->x, w->x, w->w); emit_change(w); return 1;
    }
    if (e->type == EUI_EV_MOUSEUP && d->dragging) { d->dragging = 0; return 1; }
    if (e->type == EUI_EV_MOUSEMOVE && d->dragging) {
        slider_set_from_x(d, e->x, w->x, w->w); emit_change(w); return 1;
    }
    if (e->type == EUI_EV_KEYDOWN) {
        if (e->code == EUI_SCAN_LEFT) { eui_slider_set(w, d->value - 1); emit_change(w); return 1; }
        if (e->code == EUI_SCAN_RIGHT) { eui_slider_set(w, d->value + 1); emit_change(w); return 1; }
    }
    return 0;
}

/* ── Entry ───────────────────────────────────────────────────── */
typedef struct { char text[256]; int cursor; int scroll; int secret; } entry_data;
eui_widget* eui_entry_new(void) {
    entry_data* d = (entry_data*)eigen_malloc(sizeof(entry_data));
    memset(d, 0, sizeof(*d));
    eui_widget* w = eui_widget_alloc(EUI_KIND_ENTRY, d);
    w->pref_h = 26; w->pref_w = 180; w->expand_x = 1; w->weight_x = 1;
    w->focusable = 1;
    return w;
}
void eui_entry_get(eui_widget* w, char* buf, int max) {
    if (!w || w->kind != EUI_KIND_ENTRY) { if (buf && max) buf[0]=0; return; }
    strncpy(buf, ((entry_data*)w->data)->text, max - 1); buf[max - 1] = 0;
}
void eui_entry_set(eui_widget* w, const char* t) {
    if (!w || w->kind != EUI_KIND_ENTRY) return;
    entry_data* d = (entry_data*)w->data;
    strncpy(d->text, t ? t : "", 255); d->text[255] = 0; d->cursor = eigen_strlen(d->text); w->dirty = 1;
}
void eui_entry_set_secret(eui_widget* w, int on) { if (w) ((entry_data*)w->data)->secret = on; }
void eui_entry_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_entry_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    entry_data* d = (entry_data*)w->data;
    eui_canvas_round(c, w->x, w->y, w->w, w->h, t->radius_small, t->surface_var);
    eui_canvas_round_stroke(c, w->x, w->y, w->w, w->h, t->radius_small, 1,
                            w->focused ? t->accent : t->border);
    int pad = 6;
    eui_canvas_push_clip(c, &EUI_RECT(w->x + pad, w->y, w->w - 2 * pad, w->h));
    char disp[256]; int n = 0;
    const char* src = d->text;
    for (int i = 0; src[i] && n < 255; i++) {
        if (d->secret) disp[n++] = '*'; else disp[n++] = src[i];
    }
    disp[n] = 0;
    int tw = measure_text_w(disp, t->font_size);
    int tx = w->x + pad;
    if (tw > w->w - 2 * pad) tx = w->x + pad - (tw - (w->w - 2 * pad));
    eui_font_draw(c, tx, w->y + (w->h - t->font_size) / 2, t->font_size, t->text, disp);
    if (w->focused) {
        int blink = (eigen_gettime_ms() / 500) & 1;
        if (blink) {
            int caret_x = tx + measure_text_w(disp, t->font_size);
            if (caret_x < w->x + w->w - pad)
                eui_canvas_fill_rect(c, caret_x, w->y + 5, 2, w->h - 10, t->accent);
        }
    }
    eui_canvas_pop_clip(c);
}
int eui_entry_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    entry_data* d = (entry_data*)w->data;
    if (e->type == EUI_EV_KEYDOWN) {
        if (eui_key_printable(e)) {
            int len = eigen_strlen(d->text);
            if (len < 255) { memmove(d->text + d->cursor + 1, d->text + d->cursor, len - d->cursor + 1);
                             d->text[d->cursor] = (char)e->key; d->cursor++; w->dirty = 1; }
            return 1;
        }
        if (e->code == EUI_SCAN_BACK && d->cursor > 0) {
            memmove(d->text + d->cursor - 1, d->text + d->cursor, eigen_strlen(d->text) - d->cursor + 1);
            d->cursor--; w->dirty = 1; return 1;
        }
        if (e->code == EUI_SCAN_LEFT && d->cursor > 0) { d->cursor--; w->dirty = 1; return 1; }
        if (e->code == EUI_SCAN_RIGHT) { int l = eigen_strlen(d->text); if (d->cursor < l) { d->cursor++; w->dirty = 1; } return 1; }
        if (e->code == EUI_SCAN_HOME) { d->cursor = 0; w->dirty = 1; return 1; }
        if (e->code == EUI_SCAN_END) { d->cursor = eigen_strlen(d->text); w->dirty = 1; return 1; }
        if (e->code == EUI_SCAN_ENTER) { emit_change(w); return 1; }
    }
    return 0;
}

/* ── List ────────────────────────────────────────────────────── */
typedef struct { eui_array items; int sel; int top; int row_h; int hovered; } list_data;
eui_widget* eui_list_new(void) {
    list_data* d = (list_data*)eigen_malloc(sizeof(list_data));
    memset(d, 0, sizeof(*d));
    eui_array_init(&d->items, sizeof(list_item_t));
    d->row_h = 24; d->sel = -1; d->hovered = -1;
    eui_widget* w = eui_widget_alloc(EUI_KIND_LIST, d);
    return w;
}
void eui_list_add(eui_widget* w, const char* text) {
    if (!w || w->kind != EUI_KIND_LIST) return;
    list_data* d = (list_data*)w->data;
    list_item_t* it = (list_item_t*)eui_array_push(&d->items);
    if (it) { strncpy(it->text, text ? text : "", 63); it->text[63] = 0; it->color = EUI_NONE; }
    w->pref_h = d->items.count * d->row_h;
    w->dirty = 1;
}
void eui_list_clear(eui_widget* w) {
    if (!w || w->kind != EUI_KIND_LIST) return;
    list_data* d = (list_data*)w->data; eui_array_clear(&d->items); d->sel = -1; w->dirty = 1;
}
int eui_list_get_sel(eui_widget* w) { return w && w->kind == EUI_KIND_LIST ? ((list_data*)w->data)->sel : -1; }
void eui_list_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_list_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    list_data* d = (list_data*)w->data;
    eui_canvas_push_clip(c, &EUI_RECT(w->x, w->y, w->w, w->h));
    for (int i = 0; i < d->items.count; i++) {
        list_item_t* it = &((list_item_t*)d->items.data)[i];
        int ry = w->y + i * d->row_h;
        if (i == d->sel) eui_canvas_round(c, w->x + 2, ry + 1, w->w - 4, d->row_h - 2, 4, t->accent);
        else if (i == d->hovered) eui_canvas_round(c, w->x + 2, ry + 1, w->w - 4, d->row_h - 2, 4, t->surface_var);
        eui_color col = (it->color == EUI_NONE) ? (i == d->sel ? t->on_primary : t->text) : it->color;
        eui_font_draw(c, w->x + 8, ry + (d->row_h - t->font_size) / 2, t->font_size, col, it->text);
    }
    eui_canvas_pop_clip(c);
}
int eui_list_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    list_data* d = (list_data*)w->data;
    if (e->type == EUI_EV_MOUSEDOWN && EUI_IN_RECT(e->x, e->y, w->x, w->y, w->w, w->h)) {
        int i = (e->y - w->y) / d->row_h;
        if (i >= 0 && i < d->items.count) { d->sel = i; emit_change(w); } return 1;
    }
    if (e->type == EUI_EV_MOUSEMOVE && EUI_IN_RECT(e->x, e->y, w->x, w->y, w->w, w->h)) {
        d->hovered = (e->y - w->y) / d->row_h; w->dirty = 1; return 1;
    }
    if (e->type == EUI_EV_KEYDOWN) {
        if (e->code == EUI_SCAN_UP && d->sel > 0) { d->sel--; emit_change(w); return 1; }
        if (e->code == EUI_SCAN_DOWN && d->sel < d->items.count - 1) { d->sel++; emit_change(w); return 1; }
    }
    return 0;
}

/* ── Separator ───────────────────────────────────────────────── */
typedef struct { int vertical; int thickness; } separator_data;
eui_widget* eui_separator_new(int vertical) {
    separator_data* d = (separator_data*)eigen_malloc(sizeof(separator_data));
    memset(d, 0, sizeof(*d)); d->vertical = vertical; d->thickness = 1;
    eui_widget* w = eui_widget_alloc(EUI_KIND_SEPARATOR, d);
    if (vertical) { w->pref_w = 1; w->pref_h = -1; } else { w->pref_w = -1; w->pref_h = 1; }
    w->expand_x = vertical ? 0 : 1; w->expand_y = vertical ? 1 : 0;
    return w;
}
void eui_separator_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_separator_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    separator_data* d = (separator_data*)w->data;
    if (d->vertical) eui_canvas_fill_rect(c, w->x, w->y, d->thickness, w->h, t->border);
    else eui_canvas_fill_rect(c, w->x, w->y, w->w, d->thickness, t->border);
}
int eui_separator_event(eui_widget* w, const eui_event* e, struct eui_window* win) { (void)w;(void)e;(void)win; return 0; }

/* ── Icon ────────────────────────────────────────────────────── */
typedef struct { eui_icon_id icon; eui_color color; } icon_data;
eui_widget* eui_icon_new(eui_icon_id id, eui_color col) {
    icon_data* d = (icon_data*)eigen_malloc(sizeof(icon_data));
    memset(d, 0, sizeof(*d)); d->icon = id; d->color = col;
    eui_widget* w = eui_widget_alloc(EUI_KIND_ICON, d);
    w->pref_w = 24; w->pref_h = 24;
    return w;
}
void eui_icon_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_icon_wdraw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    icon_data* d = (icon_data*)w->data;
    int s = w->w < w->h ? w->w : w->h;
    eui_color col = (d->color == EUI_NONE) ? t->text : d->color;
    eui_icon_draw(c, d->icon, w->x + (w->w - s) / 2, w->y + (w->h - s) / 2, s, col);
}
int eui_icon_event(eui_widget* w, const eui_event* e, struct eui_window* win) { (void)w;(void)e;(void)win; return 0; }

/* ── Progress ────────────────────────────────────────────────── */
typedef struct { int value, max; float shown; eui_anim* anim; } progress_data;
static void pr_anim_cb(float v, void* ud) { ((progress_data*)ud)->shown = v; }
eui_widget* eui_progress_new(int max) {
    progress_data* d = (progress_data*)eigen_malloc(sizeof(progress_data));
    memset(d, 0, sizeof(*d)); d->max = max > 0 ? max : 1; d->value = 0; d->shown = 0;
    d->anim = eui_anim_new(0, 0, 250, pr_anim_cb, d);
    eui_widget* w = eui_widget_alloc(EUI_KIND_PROGRESS, d);
    w->pref_h = 12; w->pref_w = -1; w->expand_x = 1; w->weight_x = 1;
    return w;
}
void eui_progress_set(eui_widget* w, int value) {
    if (!w || w->kind != EUI_KIND_PROGRESS) return;
    progress_data* d = (progress_data*)w->data;
    d->value = eui_clamp_i(value, 0, d->max);
    eui_anim_set(d->anim, (float)d->value / d->max); w->dirty = 1;
}
void eui_progress_layout(eui_widget* w, int x, int y, int aw, int ah) { (void)aw;(void)ah; }
void eui_progress_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    progress_data* d = (progress_data*)w->data;
    eui_canvas_round(c, w->x, w->y, w->w, w->h, w->h / 2, t->surface_var);
    int fw = (int)(w->w * d->shown);
    if (fw > 0) eui_canvas_round(c, w->x, w->y, fw, w->h, w->h / 2, t->accent);
}
int eui_progress_event(eui_widget* w, const eui_event* e, struct eui_window* win) { (void)w;(void)e;(void)win; return 0; }
