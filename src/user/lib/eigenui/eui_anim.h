/***************************************************************/
/*  EigenUI Anim — Ecore-like tween/animator engine              */
/*                                                             */
/*  Lightweight time-based tweens. The app loop ticks them each  */
/*  frame; the callback receives an eased 0..1 (or from..to)     */
/*  value and updates widget state. Used for press fades,         */
/*  progress fills, knob glides, etc.                            */
/***************************************************************/
#ifndef EUI_ANIM_H
#define EUI_ANIM_H

#include "eui_core.h"

typedef float (*eui_ease_fn)(float t);   /* t in 0..1 */
typedef void  (*eui_anim_cb)(float value, void* ud);

typedef struct eui_anim {
    struct eui_anim* next;     /* intrusive global list */
    float from, to, dur, t;
    eui_anim_cb cb; void* ud;
    eui_ease_fn ease;
    int active;
} eui_anim;

eui_anim* eui_anim_new(float from, float to, float dur_ms, eui_anim_cb cb, void* ud);
void      eui_anim_free(eui_anim* a);
void      eui_anim_start(eui_anim* a);            /* (re)start from `from` */
void      eui_anim_set(eui_anim* a, float to);    /* retarget, easing from current */
int       eui_anim_tick_all(float dt_ms);        /* returns # active (call per frame) */

/* easing presets */
float eui_ease_linear(float t);
float eui_ease_out_cubic(float t);
float eui_ease_inout_cubic(float t);
float eui_ease_out_back(float t);

#endif /* EUI_ANIM_H */
