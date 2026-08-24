/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

// Mouse button state flags
#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

// Initialize PS/2 mouse
void init_mouse(void);

// Called from IRQ12 handler — processes mouse packets
void mouse_handler(void);

// State accessors
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_buttons(void);

// Raw device deltas accumulated since the last call, then reset. Used for
// relative/mouse-look input (e.g. DOOM camera control) without edge clamping.
void mouse_get_deltas(int* dx, int* dy);

// Returns 1 if mouse has moved or button state changed since last call
int mouse_has_update(void);

// Set screen bounds for clamping
void mouse_set_bounds(int max_x, int max_y);

// ── Cursor shape & color ─────────────────────────────────────
typedef enum {
    MOUSE_CURSOR_POINTER = 0,   // Classic arrow
    MOUSE_CURSOR_CIRCLE,        // Google-style filled dot
    MOUSE_CURSOR_COUNT
} mouse_cursor_t;

// Select an active cursor shape (persisted to cfg/mouse.cfg)
void mouse_set_cursor(mouse_cursor_t t);
mouse_cursor_t mouse_get_cursor(void);
const char* mouse_cursor_name(mouse_cursor_t t);

// Custom cursor colour (0xRRGGBB, persisted to cfg/mouse.cfg)
void mouse_set_cursor_color(uint32_t rgb);
uint32_t mouse_get_cursor_color(void);

// Draw the mouse cursor (using the active shape + colour) onto the back buffer
void mouse_draw_cursor(void);
// Draw a specific shape/colour cursor centered at (cx,cy) at a scale (preview)
void mouse_draw_cursor_preview(int cx, int cy, mouse_cursor_t t,
                               uint32_t color, int scale, uint8_t glow_alpha);

// Mouse sensitivity (1=low, 2=normal, 3=high)
void mouse_set_sensitivity(int level);
int mouse_get_sensitivity(void);

// Scroll wheel
int mouse_get_wheel_delta(void);
void mouse_clear_wheel_delta(void);

// Press-edge latch: a left-button press was seen since the last frame.
// Returns 1 while a latched click is pending (position is the point where
// the press started, BEFORE the press-slide movement of the same packet).
int mouse_has_click(void);
int mouse_get_click_x(void);
int mouse_get_click_y(void);
// Consumes the latch (returns 1 and fills *x,*y if one was pending)
int mouse_consume_click(int* x, int* y);

#endif
