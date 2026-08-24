/***************************************************************/
/*  EigenUI Core — implementation                                */
/***************************************************************/
#include "eui_core.h"
#include <string.h>
#include <stdlib.h>

eui_color eui_lerp(eui_color a, eui_color b, int t, int T) {
    if (T <= 0) return b;
    if (t <= 0) return a;
    if (t >= T) return b;
    int ar = EUI_R(a), ag = EUI_G(a), ab = EUI_B(a);
    int br = EUI_R(b), bg = EUI_G(b), bb = EUI_B(b);
    int r = (ar * (T - t) + br * t) / T;
    int g = (ag * (T - t) + bg * t) / T;
    int b2 = (ab * (T - t) + bb * t) / T;
    return EUI_RGB(r, g, b2);
}

eui_color eui_lighten(eui_color c, int amt) {
    int r = EUI_R(c), g = EUI_G(c), b = EUI_B(c);
    if (amt >= 0) {
        r += (255 - r) * amt / 100;
        g += (255 - g) * amt / 100;
        b += (255 - b) * amt / 100;
    } else {
        int k = 100 + amt; /* 0..100 */
        r = r * k / 100; g = g * k / 100; b = b * k / 100;
    }
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return EUI_RGB(r, g, b);
}

eui_color eui_darken(eui_color c, int amt) {
    return eui_lighten(c, -amt);
}

eui_color eui_tint(eui_color base, eui_color accent, int amt) {
    return eui_lerp(base, accent, amt, 255);
}

/* ── intrusive list ─────────────────────────────────────────── */
void eui_list_init(eui_node* head) { head->next = head; head->prev = head; }
void eui_list_append(eui_node* head, eui_node* n) {
    n->prev = head->prev; n->next = head;
    head->prev->next = n; head->prev = n;
}
void eui_list_prepend(eui_node* head, eui_node* n) {
    n->next = head->next; n->prev = head;
    head->next->prev = n; head->next = n;
}
void eui_list_remove(eui_node* n) {
    if (n->next) n->next->prev = n->prev;
    if (n->prev) n->prev->next = n->next;
    n->next = n->prev = 0;
}

/* ── dynamic array ──────────────────────────────────────────── */
void eui_array_init(eui_array* a, size_t elem) {
    a->data = 0; a->count = 0; a->cap = 0; a->elem = elem;
}
void eui_array_free(eui_array* a) {
    if (a->data) free(a->data);
    a->data = 0; a->count = 0; a->cap = 0;
}
void* eui_array_push(eui_array* a) {
    if (a->count >= a->cap) {
        int ncap = a->cap ? a->cap * 2 : 8;
        void* nd = realloc(a->data, ncap * a->elem);
        if (!nd) return 0;
        a->data = nd; a->cap = ncap;
    }
    void* slot = (char*)a->data + (size_t)a->count * a->elem;
    memset(slot, 0, a->elem);
    a->count++;
    return slot;
}
void eui_array_clear(eui_array* a) { a->count = 0; }

/* ── math ───────────────────────────────────────────────────── */
int eui_clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int eui_min_i(int a, int b) { return a < b ? a : b; }
int eui_max_i(int a, int b) { return a > b ? a : b; }
