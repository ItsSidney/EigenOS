/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include "drivers/video/vga.h"

// Console settings
#define MAX_ROWS 24
#define MAX_COLS 64

void init_framebuffer(uint32_t* fb_address, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp,
                      uint8_t red_shift, uint8_t green_shift, uint8_t blue_shift);

void swap_buffers();
void set_mouse_pos(int x, int y);
void fill_screen_vga(int vga_color);
void draw_box_vga(int x, int y, int w, int h, int vga_color);
void draw_gradient_background();
void draw_splash_screen(int step);
void splash_anim_advance(void);
void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
void boot_log_add_hex(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color,
                       uint64_t hex_value, int hex_color_override);
void draw_boot_log(void);
void cache_background();
void restore_background();

void clear_screen();
void print_char_at(char character, int color, int x, int y);
void print_char(char character, uint32_t color);
void print_string_color(const char* message, int color);
void print_string(const char* message);
void print_backspace();

/* BMP helpers */
int fb_draw_bmp(int x, int y, const unsigned char* data, unsigned int len);
int fb_get_width(void);
int fb_get_height(void);
uint32_t* fb_get_back_buffer(void);
void set_cursor(int x, int y);
void clear_terminal_cursor();
void draw_terminal_cursor();

uint32_t get_fb_width();
uint32_t get_fb_height();
void put_pixel(uint32_t x, uint32_t y, uint32_t rgb_color);

// GFX engine bridge functions
uint32_t* gfx_get_back_buffer(void);
uint32_t  gfx_get_fb_width(void);
uint32_t  gfx_get_fb_height(void);
uint32_t  gfx_get_stride(void);
uint32_t  gfx_rgb_to_pixel(uint32_t rgb);

#endif
