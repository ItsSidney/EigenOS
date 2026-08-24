/***************************************************************/
/*  EigenUI Anim — implementation                                */
/***************************************************************/
#include "eui_anim.h"
#include <stdlib.h>

static eui_anim* g_list = 0;

eui_anim* eui_anim_new(float from, float to, float dur_ms, eui_anim_cb cb, void* ud) {
    eui_anim* a = (eui_anim*)eigen_malloc(sizeof(eui_anim));
    if (!a) return 0;
    a->next = 0; a->from = from; a->to = to; a->dur = dur_ms; a->t = 0;
    a->cb = cb; a->ud = ud; a->ease = eui_ease_out_cubic; a->active = 1;
    a->next = g_list; g_list = a;
    return a;
}
void eui_anim_free(eui_anim* a) {
    eui_anim** pp = &g_list;
    while (*pp) { if (*pp == a) { *pp = a->next; break; } pp = &(*pp)->next; }
    eigen_free(a);
}
void eui_anim_start(eui_anim* a) { if (a) { a->t = 0; a->active = 1; } }
void eui_anim_set(eui_anim* a, float to) {
    if (!a) return;
    a->from = (a->dur > 0 && a->t < a->dur) ? a->from + (a->to - a->from) * (a->t / a->dur) : a->to;
    a->to = to; a->t = 0; a->active = 1;
}
int eui_anim_tick_all(float dt_ms) {
    int active = 0;
    eui_anim* a = g_list;
    while (a) {
        eui_anim* nx = a->next;
        if (a->active) {
            active++;
            a->t += dt_ms;
            float p = a->dur > 0 ? (a->t / a->dur) : 1.0f;
            if (p >= 1.0f) { p = 1.0f; a->active = 0; }
            float e = a->ease ? a->ease(p) : p;
            float v = a->from + (a->to - a->from) * e;
            if (a->cb) a->cb(v, a->ud);
        }
        a = nx;
    }
    return active;
}

static float pow_int(int b, int e) {
    float r = 1; int neg = e < 0; if (neg) e = -e;
    for (int i = 0; i < e; i++) r *= b;
    return neg ? 1.0f / r : r;
}
float eui_ease_linear(float t) { return t; }
float eui_ease_out_cubic(float t) { float u = 1 - t; return 1 - u * u * u; }
float eui_ease_inout_cubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - pow_int(2 * (1 - t), 3) / 2.0f;
}
float eui_ease_out_back(float t) { float c1 = 1.70158f, c3 = c1 + 1; return 1 + c3 * pow_int(t - 1, 3) + c1 * pow_int(t - 1, 2); }
