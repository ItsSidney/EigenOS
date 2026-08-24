/***************************************************************/
/*  EigenUI Widget — base implementation & dispatch              */
/***************************************************************/
#include "eui_private.h"
#include <stdlib.h>
#include <string.h>

eui_widget* eui_widget_alloc(eui_widget_kind kind, void* data) {
    eui_widget* w = (eui_widget*)eigen_malloc(sizeof(eui_widget));
    if (!w) return 0;
    memset(w, 0, sizeof(*w));
    eui_list_init(&w->node);
    eui_array_init(&w->children, sizeof(eui_widget*));
    w->kind = kind;
    w->data = data;
    w->weight_x = w->weight_y = 0;
    w->align_x = w->align_y = EUI_ALIGN_FILL;
    w->margin = 0;
    w->expand_x = w->expand_y = 1;
    w->visible = 1; w->enabled = 1; w->focusable = 0; w->focused = 0; w->dirty = 1;
    w->min_w = w->min_h = 0; w->pref_w = w->pref_h = -1;
    return w;
}
void eui_widget_free(eui_widget* w) {
    if (!w) return;
    eui_list_remove(&w->node);
    eui_array_free(&w->children);
    if (w->data) eigen_free(w->data);
    if (w->tooltip) eigen_free(w->tooltip);
    eigen_free(w);
}
void eui_widget_set_rect(eui_widget* w, int x, int y, int ww, int hh) {
    w->x = x; w->y = y; w->w = ww; w->h = hh;
}

void eui_widget_set_expand(eui_widget* w, int x, int y) { w->expand_x = x; w->expand_y = y; }
void eui_widget_set_align(eui_widget* w, eui_align ax, eui_align ay) { w->align_x = ax; w->align_y = ay; }
void eui_widget_set_weight(eui_widget* w, float wx, float wy) { w->weight_x = wx; w->weight_y = wy; }
void eui_widget_set_margin(eui_widget* w, int m) { w->margin = m; }
void eui_widget_set_min_size(eui_widget* w, int mw, int mh) { w->min_w = mw; w->min_h = mh; }
void eui_widget_set_pref_size(eui_widget* w, int pw, int ph) { w->pref_w = pw; w->pref_h = ph; }
void eui_widget_set_enabled(eui_widget* w, int on) { w->enabled = on; w->dirty = 1; }
void eui_widget_set_visible(eui_widget* w, int on) { w->visible = on; w->dirty = 1; }

/* ── dispatch ─────────────────────────────────────────────────── */
void eui_widget_layout(eui_widget* w, int x, int y, int aw, int ah) {
    if (!w || !w->visible) return;
    switch (w->kind) {
    case EUI_KIND_BOX:       eui_box_layout(w, x, y, aw, ah); break;
    case EUI_KIND_PANEL:     eui_panel_layout(w, x, y, aw, ah); break;
    case EUI_KIND_LABEL:     eui_label_layout(w, x, y, aw, ah); break;
    case EUI_KIND_BUTTON:    eui_button_layout(w, x, y, aw, ah); break;
    case EUI_KIND_TOGGLE:    eui_toggle_layout(w, x, y, aw, ah); break;
    case EUI_KIND_SLIDER:    eui_slider_layout(w, x, y, aw, ah); break;
    case EUI_KIND_ENTRY:     eui_entry_layout(w, x, y, aw, ah); break;
    case EUI_KIND_LIST:      eui_list_layout(w, x, y, aw, ah); break;
    case EUI_KIND_SEPARATOR: eui_separator_layout(w, x, y, aw, ah); break;
    case EUI_KIND_ICON:      eui_icon_layout(w, x, y, aw, ah); break;
    case EUI_KIND_PROGRESS:  eui_progress_layout(w, x, y, aw, ah); break;
    case EUI_KIND_SCROLL:    eui_scroll_layout(w, x, y, aw, ah); break;
    case EUI_KIND_SPACER:    eui_spacer_layout(w, x, y, aw, ah); break;
    }
    w->x = x; w->y = y; w->w = aw; w->h = ah;
}

void eui_widget_draw(eui_widget* w, eui_canvas* c, eui_theme* t) {
    if (!w || !w->visible) return;
    switch (w->kind) {
    case EUI_KIND_BOX:       eui_box_draw(w, c, t); break;
    case EUI_KIND_PANEL:     eui_panel_draw(w, c, t); break;
    case EUI_KIND_LABEL:     eui_label_draw(w, c, t); break;
    case EUI_KIND_BUTTON:    eui_button_draw(w, c, t); break;
    case EUI_KIND_TOGGLE:    eui_toggle_draw(w, c, t); break;
    case EUI_KIND_SLIDER:    eui_slider_draw(w, c, t); break;
    case EUI_KIND_ENTRY:     eui_entry_draw(w, c, t); break;
    case EUI_KIND_LIST:      eui_list_draw(w, c, t); break;
    case EUI_KIND_SEPARATOR: eui_separator_draw(w, c, t); break;
    case EUI_KIND_ICON:      eui_icon_wdraw(w, c, t); break;
    case EUI_KIND_PROGRESS:  eui_progress_draw(w, c, t); break;
    case EUI_KIND_SCROLL:    eui_scroll_draw(w, c, t); break;
    case EUI_KIND_SPACER:    eui_spacer_draw(w, c, t); break;
    }
}

int eui_widget_event(eui_widget* w, const eui_event* e, struct eui_window* win) {
    if (!w || !w->visible) return 0;
    int r = 0;
    switch (w->kind) {
    case EUI_KIND_BOX:       r = eui_box_event(w, e, win); break;
    case EUI_KIND_PANEL:     r = eui_panel_event(w, e, win); break;
    case EUI_KIND_LABEL:     r = eui_label_event(w, e, win); break;
    case EUI_KIND_BUTTON:    r = eui_button_event(w, e, win); break;
    case EUI_KIND_TOGGLE:    r = eui_toggle_event(w, e, win); break;
    case EUI_KIND_SLIDER:    r = eui_slider_event(w, e, win); break;
    case EUI_KIND_ENTRY:     r = eui_entry_event(w, e, win); break;
    case EUI_KIND_LIST:      r = eui_list_event(w, e, win); break;
    case EUI_KIND_SEPARATOR: r = eui_separator_event(w, e, win); break;
    case EUI_KIND_ICON:      r = eui_icon_event(w, e, win); break;
    case EUI_KIND_PROGRESS:  r = eui_progress_event(w, e, win); break;
    case EUI_KIND_SCROLL:    r = eui_scroll_event(w, e, win); break;
    case EUI_KIND_SPACER:    r = eui_spacer_event(w, e, win); break;
    }
    if (!r && w->on_event) r = w->on_event(w, e, w->on_event_ud);
    return r;
}

eui_widget* eui_widget_hit(eui_widget* w, int x, int y) {
    if (!w || !w->visible) return 0;
    if (x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h) return 0;
    /* containers recurse to deepest child first */
    switch (w->kind) {
    case EUI_KIND_BOX: case EUI_KIND_PANEL: case EUI_KIND_SCROLL: {
        for (int i = 0; i < w->children.count; i++) {
            eui_widget* hit = eui_widget_hit(((eui_widget**)w->children.data)[i], x, y);
            if (hit) return hit;
        }
        return w;
    }
    default: return w;
    }
}
