/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/video/framebuffer.h"
#include "drivers/video/gpu.h"
#include "gui/font.h"
#include "kernel/log.h"
#include "kernel/mem/kheap.h"
#include "kernel/time/timer.h"
#include <gfx/splash_bmp.h>
#include <string.h>
#include <stdio.h>


static uint32_t* fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_bpp = 0;

static uint8_t fb_red_shift = 16;
static uint8_t fb_green_shift = 8;
static uint8_t fb_blue_shift = 0;

static uint32_t* back_buffer = 0;
static uint32_t internal_stride = 0;

static int cursor_x = 0;
static int cursor_y = 0;
static bool splash_mode = true;

static const uint32_t vga_palette[16] = {
    0x000000, 0x1F6FEB, 0x3FB950, 0x39D2C0, 0xF85149, 0xBC8CFF, 0xF0883E, 0x8B949E,
    0x484F58, 0x58A6FF, 0x56D364, 0x79C0FF, 0xFFA198, 0xD2A8FF, 0xE3B341, 0xF0F6FC
};

static inline uint32_t rgb_to_pixel(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    if (fb_red_shift == 0 && fb_green_shift == 0 && fb_blue_shift == 0) return (r << 16) | (g << 8) | b;
    return (r << fb_red_shift) | (g << fb_green_shift) | (b << fb_blue_shift);
}

static inline uint32_t vga_to_pixel(int vga_idx) { return rgb_to_pixel(vga_palette[vga_idx & 0x0F]); }

/* ── Public accessors for the Limine-style boot screen (src/boot/) ────────── */
uint32_t  fb_rgb_to_pixel(uint32_t rgb) { return rgb_to_pixel(rgb); }
void      fb_unpack_rgb(uint32_t px, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (fb_red_shift == 0 && fb_green_shift == 0 && fb_blue_shift == 0) {
        *r = (px >> 16) & 0xFF; *g = (px >> 8) & 0xFF; *b = px & 0xFF;
    } else {
        *r = (px >> fb_red_shift) & 0xFF;
        *g = (px >> fb_green_shift) & 0xFF;
        *b = (px >> fb_blue_shift) & 0xFF;
    }
}
void      fb_swap_buffers(void) { swap_buffers(); }

/* Transparent-background text (so labels sit on the boot image, not boxes). */
static void boot_draw_char_t(int x, int y, char c, uint32_t fg) {
    if (x < 0 || x + 8 > (int)fb_width || y < 0 || y + 16 > (int)fb_height) return;
    const uint8_t* glyph = &font8x16[(uint8_t)c * 16];
    uint32_t pix = rgb_to_pixel(fg);
    for (int gy = 0; gy < 16; gy++) {
        uint8_t row = glyph[gy];
        int py = y + gy;
        for (int gx = 0; gx < 8; gx++) {
            int px = x + gx;
            if (row & (0x80 >> gx)) back_buffer[py * internal_stride + px] = pix;
        }
    }
}
void fb_draw_text_t(int x, int y, const char* str, uint32_t fg) {
    while (*str && x + 8 < (int)fb_width) {
        boot_draw_char_t(x, y, *str, fg);
        x += 8; str++;
    }
}

static void draw_rect(int x, int y, int w, int h, uint32_t col) {
    for(int ry=y; ry<y+h; ry++)
        for(int rx=x; rx<x+w; rx++)
            if(rx>=0 && rx<(int)fb_width && ry>=0 && ry<(int)fb_height)
                back_buffer[ry * internal_stride + rx] = col;
}

void init_framebuffer(uint32_t* address, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp,
                      uint8_t red_shift, uint8_t green_shift, uint8_t blue_shift) {
    fb_addr = address;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
    fb_bpp = bpp;
    fb_red_shift = red_shift;
    fb_green_shift = green_shift;
    fb_blue_shift = blue_shift;
    internal_stride = width;

    if (back_buffer) kfree(back_buffer);
    back_buffer = (uint32_t*)kmalloc(width * height * sizeof(uint32_t));
    uint32_t bg = vga_to_pixel(VGA_COLOR_BLACK);
    for (uint32_t i = 0; i < width * height; i++) back_buffer[i] = bg;
}

void swap_buffers() {
    if (!fb_addr || !back_buffer) return;
    uint32_t* dest_row = fb_addr;
    uint32_t* src_row = back_buffer;
    for (uint32_t y = 0; y < fb_height; y++) {
        __builtin_memcpy(dest_row, src_row, fb_width * sizeof(uint32_t));
        dest_row = (uint32_t*)((uintptr_t)dest_row + fb_pitch);
        src_row += fb_width;
    }
    gpu_present();
}

static void swap_region(int x, int y, int w, int h) {
    if (!fb_addr || !back_buffer) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x + w > (int)fb_width) w = (int)fb_width - x;
    if (y + h > (int)fb_height) h = (int)fb_height - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        uint32_t* dest = (uint32_t*)((uintptr_t)fb_addr + (uintptr_t)((y + row) * fb_pitch)) + x;
        uint32_t* src = &back_buffer[(y + row) * internal_stride + x];
        __builtin_memcpy(dest, src, (size_t)w * sizeof(uint32_t));
    }
    gpu_present();
}

extern int gui_running;
extern void gui_terminal_clear(void);

void set_splash_mode(bool mode) { splash_mode = mode; }

// ============ BOOT LOG ============
#define BOOT_LOG_MAX 128
#define BOOT_HEX_MAX 24  // max hex chars to append

typedef struct {
    char tag[16];
    char msg[72];
    uint32_t tag_color;
    uint32_t msg_color;
    int has_hex;
    uint64_t hex_value;
    int hex_color;          // -1 = use msg_color, else override
} boot_entry_t;

static boot_entry_t boot_log[BOOT_LOG_MAX];
static int boot_log_count = 0;

/* Boot-log accessors for the Limine-style boot screen (src/boot/). */
int         fb_boot_log_count(void) { return boot_log_count; }
const char* fb_boot_log_msg(int i) {
    return (i >= 0 && i < boot_log_count) ? boot_log[i].msg : "";
}

static void boot_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (x < 0 || x + 8 > (int)fb_width || y < 0 || y + 16 > (int)fb_height) return;
    const uint8_t* glyph = &font8x16[(uint8_t)c * 16];
    for (int gy = 0; gy < 16; gy++) {
        uint8_t row = glyph[gy];
        int py = y + gy;
        for (int gx = 0; gx < 8; gx++) {
            int px = x + gx;
            back_buffer[py * internal_stride + px] = (row & (0x80 >> gx)) ? fg : bg;
        }
    }
}

static void boot_draw_str(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    while (*str && x + 8 < (int)fb_width) {
        boot_draw_char(x, y, *str, fg, bg);
        x += 8;
        str++;
    }
}

static int boot_strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

static void boot_draw_hex(uint64_t value, int x, int y, uint32_t fg, uint32_t bg) {
    char buf[BOOT_HEX_MAX + 4];
    int len = 0;
    buf[len++] = '0';
    buf[len++] = 'x';
    // Print 16 hex digits
    for (int i = 15; i >= 0; i--) {
        int digit = (value >> (i * 4)) & 0xF;
        buf[len++] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    buf[len++] = 0;
    boot_draw_str(x, y, buf, fg, bg);
}

void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color) {
    if (boot_log_count >= BOOT_LOG_MAX) return;
    int i = 0;
    while (tag[i] && i < 15) { boot_log[boot_log_count].tag[i] = tag[i]; i++; }
    boot_log[boot_log_count].tag[i] = 0;
    i = 0;
    while (msg[i] && i < 71) { boot_log[boot_log_count].msg[i] = msg[i]; i++; }
    boot_log[boot_log_count].msg[i] = 0;
    boot_log[boot_log_count].tag_color = tag_color;
    boot_log[boot_log_count].msg_color = msg_color;
    boot_log[boot_log_count].has_hex = 0;
    boot_log[boot_log_count].hex_value = 0;
    boot_log[boot_log_count].hex_color = -1;
    boot_log_count++;
}

void boot_log_add_hex(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color,
                       uint64_t hex_value, int hex_color_override) {
    if (boot_log_count >= BOOT_LOG_MAX) return;
    int i = 0;
    while (tag[i] && i < 15) { boot_log[boot_log_count].tag[i] = tag[i]; i++; }
    boot_log[boot_log_count].tag[i] = 0;
    i = 0;
    while (msg[i] && i < 71) { boot_log[boot_log_count].msg[i] = msg[i]; i++; }
    boot_log[boot_log_count].msg[i] = 0;
    boot_log[boot_log_count].tag_color = tag_color;
    boot_log[boot_log_count].msg_color = msg_color;
    boot_log[boot_log_count].has_hex = 1;
    boot_log[boot_log_count].hex_value = hex_value;
    boot_log[boot_log_count].hex_color = hex_color_override;
    boot_log_count++;
}

void draw_boot_log(void) {
    draw_splash_screen(boot_log_count);
}

// ── Black & white splash screen ─────────────────────────────────────────
#define SPL_BLACK 0x000000
#define SPL_WHITE 0xFFFFFF
#define SPL_LGRAY 0xD4D4D4
#define SPL_MGRAY 0x888888
#define SPL_DGRAY 0x444444
#define SPL_DIM   0x222222

static void bw_draw_letter(int x, int y, int bs, const uint8_t* pat, int bw, uint32_t col) {
    for (int row = 0; row < 7; row++)
        for (int c = 0; c < bw; c++)
            if (pat[row] & (1 << (bw - 1 - c)))
                draw_rect(x + c * bs, y + row * bs, bs, bs, col);
}

// 7x7 icon patterns for boot stages
static const uint8_t SPL_ICON_CPU[7] = {0x08,0x1C,0x22,0x2A,0x22,0x1C,0x08};
static const uint8_t SPL_ICON_MEM[7] = {0x3E,0x22,0x2A,0x2A,0x2A,0x22,0x3E};
static const uint8_t SPL_ICON_DEV[7] = {0x3E,0x22,0x22,0x22,0x3E,0x08,0x1C};
static const uint8_t SPL_ICON_GUI[7] = {0x3E,0x22,0x3E,0x22,0x22,0x22,0x3E};
static const uint8_t SPL_ICON_FS[7]  = {0x3E,0x22,0x2A,0x2A,0x2A,0x22,0x3E};

static void spl_draw_icon(int x, int y, int s, const uint8_t* pat, uint32_t col) {
    for (int row = 0; row < 7; row++)
        for (int c = 0; c < 7; c++)
            if (pat[row] & (1 << (6 - c)))
                draw_rect(x + c * s, y + row * s, s, s, col);
}

static void spl_draw_dots(int cx, int cy, int step, int n) {
    int r = 4, gap = 14;
    int ox = cx - n * gap / 2;
    for (int i = 0; i < n; i++) {
        int a = step % (n + 2) - i;
        uint32_t c = (a >= 0 && a < 2) ? rgb_to_pixel(SPL_LGRAY) : rgb_to_pixel(SPL_DIM);
        draw_rect(ox + i * gap, cy, r * 2, r * 2, c);
        if (a >= 0 && a < 2)
            draw_rect(ox + i * gap + 1, cy + 1, r * 2 - 2, r * 2 - 2, rgb_to_pixel(SPL_WHITE));
    }
}

static const char* spl_stage(const char* tag) {
    if (!tag || !tag[0]) return "";
    switch (tag[0]) {
        case 'M': return (tag[1]=='E'&&tag[2]=='M')?"Memory":"Modules";
        case 'C': return (tag[1]=='P'&&tag[2]=='U')?"Processor":"Config";
        case 'P': return (tag[1]=='C'&&tag[2]=='I')?"PCI Bus":"Platform";
        case 'A': return "ACPI";
        case 'F': return (tag[1]=='S')?"Filesystem":"Framebuffer";
        case 'K': return "Keyboard";
        case 'T': return "Timers";
        case 'G': return "Desktop";
        case 'N': return "Network";
        case 'S': return "Audio";
        case 'R': return "RAM Disk";
        default: return tag;
    }
}

static const uint8_t* spl_icon(const char* tag) {
    if (!tag || !tag[0]) return SPL_ICON_DEV;
    switch (tag[0]) {
        case 'M': return SPL_ICON_MEM;
        case 'C': return SPL_ICON_CPU;
        case 'G': return SPL_ICON_GUI;
        case 'F': return (tag[1]=='S')?SPL_ICON_FS:SPL_ICON_GUI;
        case 'P': case 'A': case 'K': case 'T': return SPL_ICON_DEV;
        case 'N': return SPL_ICON_CPU;
        case 'R': return SPL_ICON_MEM;
        case 'S': return SPL_ICON_GUI;
        default: return SPL_ICON_DEV;
    }
}

#define SPL_LOG_LINES 18

static int splash_ready = 0;
static int spl_anim = 0;
static int spl_anim_acc = 0;

/* Boot progress (0..total) driven by draw_early_progress in kernel.c. Kept
 * separate from the message count so the bar reflects the real boot stage. */
static int g_boot_progress = 0;
void boot_set_progress(int s) { g_boot_progress = s; }

static void draw_lambda_vector(int cx, int cy, int size, uint32_t color) {
    /* Draw high-res math Lambda (λ) glyph using anti-aliased geometric strokes */
    int half = size / 2;
    int thickness = size / 7;
    if (thickness < 2) thickness = 2;

    /* Main diagonal / from top-right to bottom-left */
    for (int t = -thickness/2; t <= thickness/2; t++) {
        int x0 = cx + half * 2 / 3 + t, y0 = cy - half;
        int x1 = cx - half + t, y1 = cy + half;
        /* Line drawing */
        int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
        int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;
        int x = x0, y = y0;
        while (1) {
            if (x >= 0 && x < (int)fb_width && y >= 0 && y < (int)fb_height) {
                back_buffer[y * internal_stride + x] = color;
            }
            if (x == x1 && y == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x += sx; }
            if (e2 < dx) { err += dx; y += sy; }
        }
    }

    /* Supporting leg \ branching off from center-left to bottom-right */
    for (int t = -thickness/2; t <= thickness/2; t++) {
        int x0 = cx - half / 6 + t, y0 = cy - half / 5;
        int x1 = cx + half * 4 / 5 + t, y1 = cy + half;
        int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
        int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;
        int x = x0, y = y0;
        while (1) {
            if (x >= 0 && x < (int)fb_width && y >= 0 && y < (int)fb_height) {
                back_buffer[y * internal_stride + x] = color;
            }
            if (x == x1 && y == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x += sx; }
            if (e2 < dx) { err += dx; y += sy; }
        }
    }
}

void draw_splash_screen(int step) {
    (void)step;
    if (!splash_mode) return;
    if (fb_width == 0 || fb_height == 0) return;
    /* Rendering is delegated to the dedicated Limine-style boot screen module
       (src/boot/limine_boot.c), which draws a real background image with an
       opacity overlay, a boot-entry menu, and a progress bar. */
    extern void limine_boot_render(int progress);
    limine_boot_render(g_boot_progress);
}

void splash_anim_advance(void) {
    if (!splash_mode || !splash_ready) return;
    if (++spl_anim_acc < 60) return;
    spl_anim_acc = 0;
    spl_anim++;
}


void clear_terminal_cursor() {
    print_char_at(' ', (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE, cursor_x, cursor_y);
}

void clear_screen() {
    clear_terminal_cursor();
    uint32_t bg = vga_to_pixel(VGA_COLOR_BLACK);
    for (uint32_t i = 0; i < fb_width * fb_height; i++) back_buffer[i] = bg;
    cursor_x = 0; cursor_y = 0;
}

void scroll() {
    uint32_t row_h = (fb_height / MAX_ROWS);
    if (row_h == 0) return;
    uint32_t stride = fb_width;
    uint32_t* src = back_buffer + (row_h * stride);
    uint32_t* dst = back_buffer;
    uint32_t copy_size = (MAX_ROWS - 1) * row_h * stride;
    for (uint32_t i = 0; i < copy_size; i++) dst[i] = src[i];
    uint32_t* last_row = back_buffer + ((MAX_ROWS - 1) * row_h * stride);
    uint32_t bg = vga_to_pixel(VGA_COLOR_BLACK);
    for (uint32_t i = 0; i < row_h * stride; i++) last_row[i] = bg;
    cursor_y = MAX_ROWS - 1;
}

void set_cursor(int x, int y) {
    if (x >= 0 && x < MAX_COLS) cursor_x = x;
    if (y >= 0 && y < MAX_ROWS) cursor_y = y;
}

void get_cursor(int* x, int* y) { *x = cursor_x; *y = cursor_y; }

uint32_t* get_fb_ptr() { return back_buffer; }
uint32_t get_fb_width() { return fb_width; }
uint32_t get_fb_height() { return fb_height; }
uint32_t gfx_get_stride() { return internal_stride; }

void print_char_at(char character, int color, int x, int y) {
    if (x < 0 || x >= MAX_COLS || y < 0 || y >= MAX_ROWS) return;
    uint32_t cw = (fb_width / MAX_COLS);
    uint32_t ch = (fb_height / MAX_ROWS);
    if (cw == 0 || ch == 0) return;
    uint32_t fg = vga_to_pixel(color & 0x0F);
    uint32_t bg = vga_to_pixel((color >> 4) & 0x0F);
    const uint8_t* glyph = &font8x16[(uint8_t)character * 16];
    for (uint32_t gy = 0; gy < ch; gy++) {
        uint8_t line = glyph[gy >> 1];
        uint32_t* dst = back_buffer + (((y * ch) + gy) * internal_stride) + (x * cw);
        for (uint32_t gx = 0; gx < cw; gx++) {
            dst[gx] = (line & (0x80 >> (gx >> 1))) ? fg : bg;
        }
    }
}

void print_string_color(const char* message, int color) {
    if (splash_mode) {
        extern void serial_puts(const char* s);
        serial_puts(message);
        return;
    }
    klog(message);
    
    extern void serial_puts(const char* s);
    serial_puts(message);
    
    extern int gui_running;
    if (gui_running) return;
    
    while (*message) {
        clear_terminal_cursor();
        char c = *message++;
        if (c == '\n') { cursor_x = 0; cursor_y++; }
        else { print_char_at(c, color, cursor_x, cursor_y); cursor_x++; }
        if (cursor_x >= MAX_COLS) { cursor_x = 0; cursor_y++; }
        while (cursor_y >= MAX_ROWS) scroll();
    }
}

void print_string(const char* message) { print_string_color(message, (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE); }

void print_char(char character, uint32_t color) {
    print_char_at(character, (int)color, cursor_x, cursor_y);
    cursor_x++;
    if (cursor_x >= MAX_COLS) { cursor_x = 0; cursor_y++; }
    while (cursor_y >= MAX_ROWS) scroll();
}

void print_backspace() {
    clear_terminal_cursor();
    if (cursor_x > 0) cursor_x--;
    else if (cursor_y > 0) { cursor_y--; cursor_x = MAX_COLS - 1; }
    print_char_at(' ', (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE, cursor_x, cursor_y);
}

void draw_terminal_cursor() {
    draw_box_vga(cursor_x, cursor_y, 1, 1, VGA_COLOR_WHITE);
}

void put_pixel(uint32_t x, uint32_t y, uint32_t rgb_color) {
    if (x >= fb_width || y >= fb_height) return;
    back_buffer[(y * internal_stride) + x] = rgb_to_pixel(rgb_color);
}

void draw_box_vga(int x_grid, int y_grid, int w_grid, int h_grid, int vga_color) {
    uint32_t cw = (fb_width / MAX_COLS);
    uint32_t ch = (fb_height / MAX_ROWS);
    if (cw == 0 || ch == 0) return;
    uint32_t color = vga_to_pixel(vga_color);
    for (int y = y_grid * ch; y < (y_grid + h_grid) * ch; y++) {
        uint32_t* dst = back_buffer + (y * internal_stride) + (x_grid * cw);
        for (int x = 0; x < w_grid * cw; x++) dst[x] = color;
    }
}

uint32_t* gfx_get_back_buffer() { return back_buffer; }
uint32_t gfx_get_fb_width() { return fb_width; }
uint32_t gfx_get_fb_height() { return fb_height; }
uint32_t gfx_rgb_to_pixel(uint32_t rgb) { return rgb_to_pixel(rgb); }

/* Debug: heap addresses of the back/background buffers so a crash dump can
   tell whether a faulting kernel stack overlaps them. */
uint64_t gfx_back_buffer_addr(void) { return (uint64_t)(uintptr_t)back_buffer; }
uint64_t gfx_back_buffer_size(void) { return (uint64_t)fb_width * fb_height * sizeof(uint32_t); }

static uint32_t* bg_buffer = 0;
static uint32_t bg_buffer_size = 0;
uint64_t gfx_bg_buffer_addr(void) { return (uint64_t)(uintptr_t)bg_buffer; }
void cache_background() {
    uint32_t sz = fb_width * fb_height;
    if (!bg_buffer || bg_buffer_size < sz) {
        if (bg_buffer) kfree(bg_buffer);
        bg_buffer = (uint32_t*)kmalloc(sz * sizeof(uint32_t));
        bg_buffer_size = sz;
    }
    for (uint32_t i = 0; i < sz; i++) bg_buffer[i] = back_buffer[i];
}
void restore_background() {
    if (!bg_buffer) return;
    uint32_t sz = fb_width * fb_height;
    for (uint32_t i = 0; i < sz && i < bg_buffer_size; i++) back_buffer[i] = bg_buffer[i];
}

int fb_draw_bmp(int x, int y, const unsigned char* data, unsigned int len) {
    return draw_bmp(x, y, data, len, back_buffer, internal_stride, (int)fb_width, (int)fb_height, 0, 0, 0, 0);
}

int fb_get_width(void) { return (int)fb_width; }
int fb_get_height(void) { return (int)fb_height; }
uint32_t* fb_get_back_buffer(void) { return back_buffer; }
