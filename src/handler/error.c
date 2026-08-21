#include "handler/error.h"
#include "gui/gui.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "kernel/time/timer.h"
#include <stdio.h>
#include <stdarg.h>

static char err_msg[128] = {0};
static uint32_t err_time = 0;

static int panic_state = 0;
static char panic_title[64] = {0};
static char panic_desc[256] = {0};

void kerror(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(err_msg, sizeof(err_msg), fmt, args);
    va_end(args);
    err_time = timer_get_ms();
}

void kerror_render(void) {
    if (!err_msg[0]) return;
    uint32_t now = timer_get_ms();
    if (now - err_time > 3000) {
        err_msg[0] = 0;
        return;
    }
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int bw = 400, bh = 40;
    int bx = (fw - bw) / 2, by = fh - bh - 50;
    gfx_fill_rect(bx, by, bw, bh, 0x1E1E24);
    gfx_draw_rect_outline(bx, by, bw, bh, 1, 0xEF4444);
    gfx_draw_string_transparent(bx + 12, by + 12, err_msg, 0xFFFFFF);
}

void kerror_clear(void) {
    err_msg[0] = 0;
}

int kpanic_active(void) {
    return panic_state;
}

void kpanic_trigger(const char* title, const char* msg) {
    panic_state = 1;
    snprintf(panic_title, sizeof(panic_title), "%s", title ? title : "Kernel Panic");
    snprintf(panic_desc, sizeof(panic_desc), "%s", msg ? msg : "An unrecoverable system error occurred.");
}

void kpanic_render(void) {
    if (!panic_state) return;
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int pw = 500, ph = 240;
    int px = (fw - pw) / 2, py = (fh - ph) / 2;
    gfx_fill_rect(px, py, pw, ph, 0x11161D);
    gfx_draw_rect_outline(px, py, pw, ph, 2, 0xF85149);
    gfx_fill_rect(px, py, pw, 32, 0x7E1D1D);
    gfx_draw_string_transparent(px + 16, py + 8, panic_title, 0xFFFFFF);
    gfx_draw_string_transparent(px + 16, py + 50, panic_desc, 0xE6EDF3);
    gfx_draw_string_transparent(px + 16, py + 120, "Click Dismiss to attempt desktop recovery.", 0x8B949E);
    
    int btn_x = px + (pw - 100) / 2, btn_y = py + ph - 45;
    gfx_fill_rect(btn_x, btn_y, 100, 30, 0x21262D);
    gfx_draw_rect_outline(btn_x, btn_y, 100, 30, 1, 0x30363D);
    gfx_draw_string_transparent(btn_x + 24, btn_y + 8, "Dismiss", 0xFFFFFF);
}

void kpanic_handle_click(int mx, int my, int clicked) {
    if (!panic_state || !clicked) return;
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    int pw = 500, ph = 240;
    int px = (fw - pw) / 2, py = (fh - ph) / 2;
    int btn_x = px + (pw - 100) / 2, btn_y = py + ph - 45;
    if (mx >= btn_x && mx <= btn_x + 100 && my >= btn_y && my <= btn_y + 30) {
        panic_state = 0;
    }
}
