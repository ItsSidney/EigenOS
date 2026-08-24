/***************************************************************/
/*  EigenUI Theme — Edje-like look & feel (EigenUI Theme)        */
/*                                                             */
/*  A theme is a value struct of colours + metrics. Apps can     */
/*  customise the global default or a single window's theme.     */
/*  The default is Haiku-inspired: clean surfaces, soft shadows, */
/*  rounded controls, a signature blue accent.                  */
/***************************************************************/
#ifndef EUI_THEME_H
#define EUI_THEME_H

#include "eui_core.h"

#define EUI_NONE 0xFFFFFFFFu   /* "inherit / not set" sentinel */

typedef struct eui_theme {
    /* palette */
    eui_color bg;          /* window background                */
    eui_color surface;      /* raised surface / cards          */
    eui_color surface_var;  /* sunken panel                    */
    eui_color text;         /* primary text                    */
    eui_color on_primary;   /* text on accent                  */
    eui_color dim;          /* secondary text                  */
    eui_color faint;        /* hint / disabled text            */
    eui_color accent;       /* primary accent                  */
    eui_color accent2;      /* accent highlight                */
    eui_color good;         /* success                         */
    eui_color bad;          /* danger                          */
    eui_color warning;      /* warning                         */
    eui_color border;       /* control outline                 */
    eui_color shadow;       /* shadow colour (alpha added)     */

    /* metrics (logical px) */
    int radius;             /* control corner radius           */
    int radius_small;       /* inner / small radius            */
    int padding;            /* default inner padding           */
    int border_width;       /* outline width                   */
    int shadow_blur;        /* drop-shadow spread              */
    int shadow_off;         /* drop-shadow y offset            */
    int font_size;          /* body text                       */
    int font_size_small;    /* captions                        */
    int font_size_large;    /* headers                         */
    int control_h;          /* standard control height         */
} eui_theme;

/* Mutable global default theme (initialised to the Haiku preset). */
eui_theme* eui_theme_default(void);
void       eui_theme_set_default(const eui_theme* t);

/* Populate a theme from the live OS palette (eigen_win_gettheme indices). */
void eui_theme_from_os(eui_theme* t, const uint32_t* os, int n);

/* A sensible dark and a light preset, handy for quick switches. */
void eui_theme_preset_light(eui_theme* t);
void eui_theme_preset_dark(eui_theme* t);

#endif /* EUI_THEME_H */
