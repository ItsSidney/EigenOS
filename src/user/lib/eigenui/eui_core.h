/***************************************************************/
/*                                                             */
/*  EigenUI — Core types, colour math, geometry, lists         */
/*  Layer: Eina-like (EigenUI Core)                            */
/*                                                             */
/*  EigenOS ring-3 GUI toolkit. See eigenui.h for overview.    */
/*                                                             */
/***************************************************************/
#ifndef EUI_CORE_H
#define EUI_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <userlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Boolean ───────────────────────────────────────────────── */
typedef enum { EUI_FALSE = 0, EUI_TRUE = 1 } eui_bool;

/* ── Colour (0x00RRGGBB, matching the framebuffer) ─────────── */
typedef uint32_t eui_color;
#define EUI_RGB(r,g,b)   ((eui_color)(((r)<<16)|((g)<<8)|(b)))
#define EUI_R(c)         (((c)>>16)&0xFF)
#define EUI_G(c)         (((c)>>8)&0xFF)
#define EUI_B(c)         ((c)&0xFF)

/* Blend src over dst with coverage a (0..255). Result is 0xRRGGBB. */
static inline eui_color eui_blend(eui_color dst, eui_color src, uint8_t a) {
    int dr = EUI_R(dst), dg = EUI_G(dst), db = EUI_B(dst);
    int sr = EUI_R(src), sg = EUI_G(src), sb = EUI_B(src);
    int r = (sr * a + dr * (255 - a)) / 255;
    int g = (sg * a + dg * (255 - a)) / 255;
    int b = (sb * a + db * (255 - a)) / 255;
    return EUI_RGB(r, g, b);
}

/* Linear interpolate t in [0,T] between two opaque colours. */
eui_color eui_lerp(eui_color a, eui_color b, int t, int T);
eui_color eui_lighten(eui_color c, int amt);   /* amt -100..100 */
eui_color eui_darken(eui_color c, int amt);
/* Push a colour toward an accent by amt (0..255). */
eui_color eui_tint(eui_color base, eui_color accent, int amt);

/* ── Geometry ──────────────────────────────────────────────── */
typedef struct { int x, y, w, h; } eui_rect;
typedef struct { int w, h; }        eui_size;
typedef struct { int x, y; }        eui_point;

#define EUI_RECT(x,y,w,h)   ((eui_rect){ (x), (y), (w), (h) })
#define EUI_PT_IN_RECT(px,py,r)  ((px)>=(r).x && (px)<(r).x+(r).w && (py)>=(r).y && (py)<(r).y+(r).h)
#define EUI_IN_RECT(px,py,x,y,w,h) ((px)>=(x) && (px)<(x)+(w) && (py)>=(y) && (py)<(y)+(h))

/* ── Alignment (used by the layout engine) ─────────────────── */
typedef enum {
    EUI_ALIGN_START = 0,   /* left / top    */
    EUI_ALIGN_CENTER,      /* centred       */
    EUI_ALIGN_END,         /* right / bottom*/
    EUI_ALIGN_FILL         /* stretch       */
} eui_align;

/* ── Intrusive doubly-linked list (Eina-like) ──────────────── */
typedef struct eui_node {
    struct eui_node* next;
    struct eui_node* prev;
} eui_node;

void  eui_list_init(eui_node* head);
void  eui_list_append(eui_node* head, eui_node* n);
void  eui_list_prepend(eui_node* head, eui_node* n);
void  eui_list_remove(eui_node* n);
#define EUI_LIST_FOREACH(head, type, node, member)                 \
    for ((node) = (type*)((head).next);                           \
         &((node)->member) != &(head);                            \
         (node) = (type*)((node)->member.next))

/* ── Tiny dynamic array (Eina-like inline array) ───────────── */
typedef struct {
    void*  data;
    int    count;
    int    cap;
    size_t elem;
} eui_array;

void  eui_array_init(eui_array* a, size_t elem);
void  eui_array_free(eui_array* a);
void* eui_array_push(eui_array* a);          /* returns ptr to new slot */
void  eui_array_clear(eui_array* a);

/* ── Math helpers ──────────────────────────────────────────── */
int  eui_clamp_i(int v, int lo, int hi);
int  eui_min_i(int a, int b);
int  eui_max_i(int a, int b);

/* ── Opaque forward decls (resolved by later layers) ───────── */
typedef struct eui_widget  eui_widget;
typedef struct eui_canvas  eui_canvas;
typedef struct eui_theme   eui_theme;
typedef struct eui_event   eui_event;
typedef struct eui_window  eui_window;

#ifdef __cplusplus
}
#endif
#endif /* EUI_CORE_H */
