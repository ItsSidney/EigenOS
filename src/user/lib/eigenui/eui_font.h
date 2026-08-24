/***************************************************************/
/*  EigenUI Font — FreeType text engine (EigenUI Font)          */
/*                                                             */
/*  Loads the "DejaVuSans" boot module and rasterises strings   */
/*  straight into the canvas with correct alpha blending. All   */
/*  sizes are logical; the canvas scale is applied.            */
/***************************************************************/
#ifndef EUI_FONT_H
#define EUI_FONT_H

#include "eui_canvas.h"

/* Call once at startup (after a window exists). Returns 0 on success. */
int  eui_font_init(void);

/* Draw `s` with its top-left at (x,y), height `size` logical px. Returns the
   advance width in logical px. */
int  eui_font_draw(eui_canvas* c, int x, int y, int size, eui_color col, const char* s);

/* Measure `s` at `size` logical px. *w/*h receive logical dimensions. */
void eui_font_measure(eui_canvas* c, const char* s, int size, int* w, int* h);

#endif /* EUI_FONT_H */
