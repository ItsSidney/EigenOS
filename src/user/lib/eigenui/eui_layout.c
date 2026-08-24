/***************************************************************/
/*  EigenUI Layout — implementation                             */
/***************************************************************/
#include "eui_private.h"
#include "eui_icons.h"
#include "eui_font.h"
#include "eui_window.h"
#include <stdlib.h>
#include <string.h>

/* ── shared helpers ─────────────────────────────────────────── */
typedef struct { int dir; int spacing; int padding; } box_data;
typedef struct { eui_widget* content; int scroll_x, scroll_y; int ext_w, ext_h;
                 int drag_x, drag_y, dragging, sb, vertical; } scroll_data;
typedef struct { char title[64]; int title_h; int padding; } panel_data;

static int desired_main(eui_widget* c, int axis) {
    int p = axis ? c->pref_h : c->pref_w;
    if (p >= 0) return p;
    int m = axis ? c->min_h : c->min_w;
    return m > 0 ? m : 24;
}
static int desired_cross(eui_widget* c, int axis) {
    int p = axis ? c->pref_w : c->pref_h;
    if (p >= 0) return p;
    int m = axis ? c->min_w : c->min_h;
    return m > 0 ? m : 24;
}

/* flow layout for a children array (vertical=1). Returns used main extent. */
static int flow_layout(eui_array* kids, int inner_x, int inner_y, int inner_w, int inner_h,
                       int vertical, int spacing) {
    int n = 0;
    for (int i = 0; i < kids->count; i++) {
        eui_widget* c = ((eui_widget**)kids->data)[i];
        if (c->visible) n++;
    }
    if (n == 0) return 0;

    int* dm = (int*)eigen_malloc(sizeof(int) * n);
    eui_widget** kl = (eui_widget**)eigen_malloc(sizeof(void*) * n);
    float* wg = (float*)eigen_malloc(sizeof(float) * n);
    int total = 0, k = 0; float wsum = 0;
    for (int i = 0; i < kids->count; i++) {
        eui_widget* c = ((eui_widget**)kids->data)[i];
        if (!c->visible) continue;
        int m = desired_main(c, vertical) + 2 * c->margin;
        float w = vertical ? c->weight_y : c->weight_x;
        dm[k] = m; kl[k] = c; wg[k] = w > 0 ? w : 0;
        if (wg[k] > 0) wsum += wg[k]; else total += m;
        k++;
    }
    int avail = vertical ? inner_h : inner_w;
    int free = avail - total - spacing * (n - 1);
    if (free < 0) free = 0;
    int pos = vertical ? inner_y : inner_x;
    int used = 0;
    for (int i = 0; i < n; i++) {
        eui_widget* c = kl[i];
        int alloc = dm[i];
        if (wg[i] > 0 && wsum > 0) alloc = dm[i] + (int)(free * (wg[i] / wsum));
        int cell_main = alloc - 2 * c->margin;
        int cell_cross = (vertical ? inner_w : inner_h) - 2 * c->margin;
        if (!(vertical ? c->expand_x : c->expand_y)) {
            int dc = desired_cross(c, vertical);
            cell_cross = dc;
        }
        int cross_off = 0;
        eui_align ca = vertical ? c->align_x : c->align_y;
        int full_cross = (vertical ? inner_w : inner_h) - 2 * c->margin;
        if (ca == EUI_ALIGN_CENTER) cross_off = (full_cross - cell_cross) / 2;
        else if (ca == EUI_ALIGN_END) cross_off = full_cross - cell_cross;
        else if (ca == EUI_ALIGN_FILL) { cell_cross = full_cross; cross_off = 0; }

        int cx, cy;
        if (vertical) { cx = inner_x + c->margin + cross_off; cy = pos + c->margin; }
        else          { cx = pos + c->margin; cy = inner_y + c->margin + cross_off; }

        eui_widget_layout(c, cx, cy, cell_cross, cell_main);
        pos += alloc + spacing;
        int end = vertical ? (cy + cell_main) : (cx + cell_cross);
        if (end - (vertical ? inner_y : inner_x) > used) used = end - (vertical ? inner_y : inner_x);
    }
    eigen_free(dm); eigen_free(kl); eigen_free(wg);
    return used;
}

/* ── Box ─────────────────────────────────────────────────────── */
eui_widget* eui_box_new(int dir) {
    box_data* d = (box_data*)eigen_malloc(sizeof(box_data));
    memset(d, 0, sizeof(*d));
    d->dir = dir; d->spacing = 6; d->padding = 0;
    eui_widget* w = eui_widget_alloc(EUI_KIND_BOX, d);
    w->weight_x = w->weight_y = 1; w->expand_x = w->expand_y = 1;
    return w;
}
void eui_box_add(eui_widget* box, eui_widget* child) {
    if (!box || !child || box->kind != EUI_KIND_BOX) return;
    eui_widget** slot = (eui_widget**)eui_array_push(&box->children);
    if (slot) { *slot = child; child->win = box->win; box->dirty = 1; }
}
void eui_box_set_spacing(eui_widget* box, int s) { if (box) ((box_data*)box->data)->spacing = s; }
void eui_box_set_padding(eui_widget* box, int p) { if (box) ((box_data*)box->data)->padding = p; }

void eui_box_layout(eui_widget* w, int x, int y, int aw, int ah) {
    box_data* d = (box_data*)w->data;
    int pm = d->padding;
    int ix = x + pm, iy = y + pm, iw = aw - 2 * pm, ih = ah - 2 * pm;
    if (iw < 0) iw = 0; if (ih < 0) ih = 0;
    int used = flow_layout(&w->children, ix, iy, iw, ih, d->dir == 0, d->spacing);
    w->extent = used + 2 * pm;
}
void eui_box_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    for (int i = 0; i < w->children.count; i++)
        eui_widget_draw(((eui_widget**)w->children.data)[i], c, t);
}
int eui_box_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    for (int i = 0; i < w->children.count; i++)
        if (eui_widget_event(((eui_widget**)w->children.data)[i], e, win)) return 1;
    return 0;
}

/* ── Panel ───────────────────────────────────────────────────── */
eui_widget* eui_panel_new(const char* title) {
    panel_data* d = (panel_data*)eigen_malloc(sizeof(panel_data));
    memset(d, 0, sizeof(*d));
    d->title_h = 22; d->padding = 8;
    if (title) { strncpy(d->title, title, 63); d->title[63] = 0; }
    eui_widget* w = eui_widget_alloc(EUI_KIND_PANEL, d);
    return w;
}
void eui_panel_add(eui_widget* panel, eui_widget* child) {
    if (!panel || !child || panel->kind != EUI_KIND_PANEL) return;
    eui_widget** slot = (eui_widget**)eui_array_push(&panel->children);
    if (slot) { *slot = child; child->win = panel->win; panel->dirty = 1; }
}
void eui_panel_set_padding(eui_widget* panel, int p) { if (panel) ((panel_data*)panel->data)->padding = p; }

void eui_panel_layout(eui_widget* w, int x, int y, int aw, int ah) {
    panel_data* d = (panel_data*)w->data;
    int pm = d->padding;
    int ix = x + pm, iy = y + d->title_h + pm, iw = aw - 2 * pm, ih = ah - d->title_h - 2 * pm;
    if (iw < 0) iw = 0; if (ih < 0) ih = 0;
    w->extent = flow_layout(&w->children, ix, iy, iw, ih, 1, 4) + d->title_h + 2 * pm;
}
void eui_panel_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    panel_data* d = (panel_data*)w->data;
    eui_canvas_round(c, w->x, w->y, w->w, w->h, t->radius, t->surface);
    eui_canvas_round_stroke(c, w->x, w->y, w->w, w->h, t->radius, t->border_width, t->border);
    if (d->title[0]) {
        eui_canvas_round_a(c, w->x, w->y, w->w, d->title_h, t->radius, t->accent, 220);
        eui_font_draw(c, w->x + 10, w->y + 4, t->font_size_small, t->on_primary, d->title);
    }
    eui_canvas_push_clip(c, &EUI_RECT(w->x, w->y, w->w, w->h));
    for (int i = 0; i < w->children.count; i++)
        eui_widget_draw(((eui_widget**)w->children.data)[i], c, t);
    eui_canvas_pop_clip(c);
}
int eui_panel_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    for (int i = 0; i < w->children.count; i++)
        if (eui_widget_event(((eui_widget**)w->children.data)[i], e, win)) return 1;
    return 0;
}

/* ── ScrollView (auto scrollbars) ────────────────────────────── */
eui_widget* eui_scroll_new(int vertical) {
    scroll_data* d = (scroll_data*)eigen_malloc(sizeof(scroll_data));
    memset(d, 0, sizeof(*d)); d->sb = 10; d->vertical = vertical;
    eui_widget* w = eui_widget_alloc(EUI_KIND_SCROLL, d);
    w->expand_x = 1; w->expand_y = 1; w->weight_x = 1; w->weight_y = 1;
    return w;
}
void eui_scroll_set_content(eui_widget* scroll, eui_widget* content) {
    if (!scroll || scroll->kind != EUI_KIND_SCROLL || !content) return;
    eui_widget** slot = (eui_widget**)eui_array_push(&scroll->children);
    if (slot) { *slot = content; content->win = scroll->win; }
    scroll->dirty = 1;
}
void eui_scroll_layout(eui_widget* w, int x, int y, int aw, int ah) {
    scroll_data* d = (scroll_data*)w->data;
    eui_widget* content = w->children.count ? ((eui_widget**)w->children.data)[0] : 0;
    if (!content) return;
    int vw = aw, vh = ah;
    /* measure natural content size */
    eui_widget_layout(content, 0, 0, vw, 1 << 20);
    d->ext_w = content->w; d->ext_h = content->extent;
    int need_v = d->ext_h > vh;
    int need_h = d->ext_w > vw;
    if (need_v) vw -= d->sb;
    if (need_h) vh -= d->sb;
    eui_widget_layout(content, 0, 0, vw, vh);
    d->ext_w = content->w; d->ext_h = content->extent;
    if (d->scroll_y > d->ext_h - vh) d->scroll_y = (d->ext_h - vh) > 0 ? d->ext_h - vh : 0;
    if (d->scroll_x > d->ext_w - vw) d->scroll_x = (d->ext_w - vw) > 0 ? d->ext_w - vw : 0;
    if (d->scroll_y < 0) d->scroll_y = 0; if (d->scroll_x < 0) d->scroll_x = 0;
}
void eui_scroll_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    scroll_data* d = (scroll_data*)w->data;
    eui_widget* content = w->children.count ? ((eui_widget**)w->children.data)[0] : 0;
    if (!content) return;
    eui_canvas_push_clip(c, &EUI_RECT(w->x, w->y, w->w, w->h));
    eui_widget_layout(content, w->x - d->scroll_x, w->y - d->scroll_y, content->w, content->h);
    eui_widget_draw(content, c, t);
    eui_canvas_pop_clip(c);
    int vw = w->w, vh = w->h;
    int need_v = d->ext_h > vh, need_h = d->ext_w > vw;
    if (need_v) vw -= d->sb; if (need_h) vh -= d->sb;
    if (need_v) {
        int track = w->h - (need_h ? d->sb : 0);
        int thumb = track * vh / d->ext_h;
        int pos = track * d->scroll_y / d->ext_h;
        eui_canvas_fill_rect(c, w->x + w->w - d->sb, w->y, d->sb, w->h - (need_h?d->sb:0), t->surface_var);
        eui_canvas_round(c, w->x + w->w - d->sb + 2, w->y + pos, d->sb - 4, thumb, t->radius_small, t->dim);
    }
    if (need_h) {
        int track = w->w - (need_v ? d->sb : 0);
        int thumb = track * vw / d->ext_w;
        int pos = track * d->scroll_x / d->ext_w;
        eui_canvas_fill_rect(c, w->x, w->y + w->h - d->sb, w->w - (need_v?d->sb:0), d->sb, t->surface_var);
        eui_canvas_round(c, w->x + pos, w->y + w->h - d->sb + 2, thumb, d->sb - 4, t->radius_small, t->dim);
    }
}
int eui_scroll_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    scroll_data* d = (scroll_data*)w->data;
    eui_widget* content = w->children.count ? ((eui_widget**)w->children.data)[0] : 0;
    if (!content) return 0;
    int vw = w->w, vh = w->h;
    int need_v = d->ext_h > vh, need_h = d->ext_w > vw;
    if (need_v) vw -= d->sb; if (need_h) vh -= d->sb;

    if (e->type == EUI_EV_MOUSEDOWN && EUI_IN_RECT(e->x, e->y, w->x, w->y, w->w, w->h)) {
        if (need_v && e->x >= w->x + w->w - d->sb) { d->dragging = 2; d->drag_x = e->x; d->drag_y = e->y; return 1; }
        if (need_h && e->y >= w->y + w->h - d->sb) { d->dragging = 3; d->drag_x = e->x; d->drag_y = e->y; return 1; }
        d->dragging = 1; d->drag_x = e->x; d->drag_y = e->y;
        return 1;
    }
    if (e->type == EUI_EV_MOUSEUP) { d->dragging = 0; return 1; }
    if (e->type == EUI_EV_MOUSEMOVE && d->dragging) {
        int dx = e->x - d->drag_x, dy = e->y - d->drag_y;
        d->drag_x = e->x; d->drag_y = e->y;
        if (d->dragging == 1) { d->scroll_x -= dx; d->scroll_y -= dy; }
        else if (d->dragging == 2) { int track = w->h - (need_h ? d->sb : 0); d->scroll_y = d->scroll_y + dy * d->ext_h / track; }
        else if (d->dragging == 3) { int track = w->w - (need_v ? d->sb : 0); d->scroll_x = d->scroll_x + dx * d->ext_w / track; }
        if (d->scroll_x < 0) d->scroll_x = 0;
        if (d->scroll_y < 0) d->scroll_y = 0;
        if (d->scroll_x > d->ext_w - vw) d->scroll_x = (d->ext_w - vw) > 0 ? d->ext_w - vw : 0;
        if (d->scroll_y > d->ext_h - vh) d->scroll_y = (d->ext_h - vh) > 0 ? d->ext_h - vh : 0;
        w->dirty = 1;
        return 1;
    }
    return eui_widget_event(content, e, win);
}

/* ── Spacer ──────────────────────────────────────────────────── */
eui_widget* eui_spacer_new(void) {
    eui_widget* w = eui_widget_alloc(EUI_KIND_SPACER, 0);
    w->weight_x = w->weight_y = 1; w->expand_x = w->expand_y = 1;
    return w;
}
void eui_spacer_layout(eui_widget* w, int x, int y, int aw, int ah) { w->extent = ah; }
void eui_spacer_draw(eui_widget* w, eui_canvas* c, eui_theme* t) { (void)w;(void)c;(void)t; }
int eui_spacer_event(eui_widget* w, const eui_event* e, struct eui_window* win) { (void)w;(void)e;(void)win; return 0; }
