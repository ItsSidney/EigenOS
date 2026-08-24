/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/***************************************************************/

#ifndef FTFONT_H
#define FTFONT_H

#include <stdint.h>

/* ── Kernel-side FreeType text engine ─────────────────────────
 * Loads the DejaVuSans Limine boot module ("DejaVuSans") and
 * renders anti-aliased text through gfx_blend_pixel. A glyph
 * cache keyed by (glyph_index, pixel_size) keeps per-frame cost
 * to a blit. All functions are no-ops until ftfont_init()
 * succeeds; they are safe to call every frame either way.     */

/* Lazy init: finds the font module and builds the FT face.
   Returns 1 on success (subsequent calls are cheap), 0 if the
   module is missing (text functions then draw nothing).       */
int  ftfont_init(void);
int  ftfont_ready(void);

/* Metrics (px = requested pixel size). */
int  ftfont_height(int px);                 /* line height      */
int  ftfont_ascent(int px);                 /* ascent above base*/
int  ftfont_width(const char* s, int px);   /* advance width    */

/* Draw UTF-8/ASCII string at TOP-LEFT (x, y) of the line box. */
void ftfont_draw(int x, int y, const char* s, uint32_t rgb, int px);

/* Draw truncated with a trailing ".." so it fits max_w.
   Returns the x just past the last drawn pixel column.        */
int  ftfont_draw_trunc(int x, int y, int max_w, const char* s,
                       uint32_t rgb, int px);

#endif /* FTFONT_H */
