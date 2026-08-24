/***************************************************************/
/*  EigenUI Widget — base class & dispatch (Elementary-like)      */
/*                                                             */
/*  Every visible thing is an eui_widget. Layout is computed by  */
/*  containers (Box/Scroll/Panel) which assign each child an      */
/*  absolute, NON-OVERLAPPING rect — overlap is structurally     */
/*  impossible by design (see eui_layout).                       */
/***************************************************************/
#ifndef EUI_WIDGET_H
#define EUI_WIDGET_H

#include "eui_core.h"
#include "eui_event.h"
#include "eui_canvas.h"
#include "eui_theme.h"

struct eui_window;

typedef enum {
    EUI_KIND_BOX = 0,   /* layout container (see eui_layout)   */
    EUI_KIND_PANEL,     /* framed group / card                */
    EUI_KIND_LABEL,     /* static text                       */
    EUI_KIND_BUTTON,    /* push button (optional icon)       */
    EUI_KIND_TOGGLE,    /* check box OR switch               */
    EUI_KIND_SLIDER,    /* horizontal value selector         */
    EUI_KIND_ENTRY,     /* single-line text input            */
    EUI_KIND_LIST,      /* selectable rows (auto-scroll)     */
    EUI_KIND_SEPARATOR, /* divider line                     */
    EUI_KIND_ICON,      /* vector icon                      */
    EUI_KIND_PROGRESS,  /* determinate bar                  */
    EUI_KIND_SCROLL,    /* auto-scroll viewport             */
    EUI_KIND_SPACER     /* flexible/empty gap               */
} eui_widget_kind;

typedef struct eui_widget {
    eui_node          node;        /* link in parent's child list     */
    eui_widget_kind   kind;
    struct eui_window* win;        /* owning window (set on add)      */

    int x, y, w, h;                /* absolute rect (logical)         */
    int min_w, min_h, pref_w, pref_h;
    float weight_x, weight_y;      /* layout stretch (0 = fixed)      */
    eui_align align_x, align_y;    /* within allotted space           */
    int margin;                    /* uniform outer margin            */
    int expand_x, expand_y;        /* fill allotted space?            */
    int visible, enabled, focusable, focused, hovered, dirty;
    int extent;                   /* used main-axis size (for scroll) */

    eui_array children;          /* container children (BOX/PANEL/SCROLL) */

    void* data;                   /* kind-specific state             */

    /* callbacks */
    void (*on_click)(struct eui_widget*, void*);   void* on_click_ud;
    void (*on_change)(struct eui_widget*, void*);  void* on_change_ud;
    int  (*on_event)(struct eui_widget*, const eui_event*, void*); void* on_event_ud;

    char* tooltip;
    void* user;
} eui_widget;

/* Base lifecycle */
eui_widget* eui_widget_alloc(eui_widget_kind kind, void* data);
void        eui_widget_free(eui_widget* w);
void        eui_widget_set_rect(eui_widget* w, int x, int y, int ww, int h);

/* Layout hints (fluent-ish) */
void eui_widget_set_expand(eui_widget* w, int x, int y);
void eui_widget_set_align(eui_widget* w, eui_align ax, eui_align ay);
void eui_widget_set_weight(eui_widget* w, float wx, float wy);
void eui_widget_set_margin(eui_widget* w, int m);
void eui_widget_set_min_size(eui_widget* w, int mw, int mh);
void eui_widget_set_pref_size(eui_widget* w, int pw, int ph);
void eui_widget_set_enabled(eui_widget* w, int on);
void eui_widget_set_visible(eui_widget* w, int on);

/* Layout pass: assign absolute rects. `avail` is the space granted to this
   widget; children fill it. Returns preferred size for containers. */
void eui_widget_layout(eui_widget* w, int x, int y, int avail_w, int avail_h);
/* Draw pass. */
void eui_widget_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
/* Event dispatch. Returns 1 if the event was consumed. */
int  eui_widget_event(eui_widget* w, const eui_event* e, struct eui_window* win);
/* Hit-test: deepest visible widget containing (x,y). */
eui_widget* eui_widget_hit(eui_widget* w, int x, int y);

/* Convenience callback wiring (defined as inline so they work without a .c). */
static inline void eui_widget_on_click(eui_widget* w, void (*cb)(eui_widget*, void*), void* ud) {
    if (w) { w->on_click = cb; w->on_click_ud = ud; }
}
static inline void eui_widget_on_change(eui_widget* w, void (*cb)(eui_widget*, void*), void* ud) {
    if (w) { w->on_change = cb; w->on_change_ud = ud; }
}

#endif /* EUI_WIDGET_H */
