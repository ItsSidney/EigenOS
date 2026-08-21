/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef APP_ICONS_H
#define APP_ICONS_H

#include <stdint.h>

void draw_app_icon(const char* name, int x, int y);
void draw_search_icon(int x, int y, int w, int h);
/* Rounded icon tile of `size` px with the 24px glyph centered.
   state: 0 normal, 1 hover, 2 selected. acc = accent colour. */
void draw_app_icon_tile(const char* name, int x, int y, int size, int state, uint32_t acc);
/* macOS-style circular icon tile: soft drop shadow + round badge + glyph.
   state: 0 normal, 1 hover, 2 selected. acc = accent colour. */
void draw_app_icon_tile_circular(const char* name, int x, int y, int size, int state, uint32_t acc);

#endif
