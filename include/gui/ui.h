/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef UI_H
#define UI_H

#include <stdint.h>

#define UI_MAX_BUTTONS 32

typedef void (*ui_click_cb)(int id);

typedef struct {
    int id;
    int x, y, w, h;
    char label[32];
    uint32_t bg_color;
    uint32_t fg_color;
    ui_click_cb on_click;
    int is_hovered;
    int is_active;
} ui_button_t;

typedef struct {
    int x, y, w, h;
    char title[64];
    uint32_t accent_color;
    int is_square;
    
    ui_button_t buttons[UI_MAX_BUTTONS];
    int button_count;
    
    void (*on_render)(int win_x, int win_y, int win_w, int win_h);
    void (*on_key)(char key);
    
    int is_running;
} ui_context_t;

void ui_init_context(ui_context_t* ctx, int x, int y, int w, int h, const char* title, uint32_t accent, int is_square);
void ui_add_button(ui_context_t* ctx, int id, int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg, ui_click_cb cb);
void ui_set_button_active(ui_context_t* ctx, int id, int active);

// Standard single-app blocking loop
void ui_app_run(ui_context_t* ctx);

#endif
