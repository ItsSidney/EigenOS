/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — PS/2 Mouse Driver
//  Robust initialization compatible with QEMU, Boxes, VBox
//  Handles mouse IRQ12, packet parsing, and cursor rendering
// ============================================================
#include "drivers/input/mouse.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/keyboard.h"  // for port_byte_in/out
#include "filesystem/filesystem.h"   // fs_mkdir/fs_create/fs_open/fs_read/fs_delete
#include <stdio.h>    // snprintf
#include <stdlib.h>   // atoi / strtoul
#include <string.h>   // strchr

static void mouse_load_theme_cfg(void);   /* defined later, in the cursor-theme section */

// ── Mouse state ─────────────────────────────────────────────
static volatile int mouse_x = 512;
static volatile int mouse_y = 384;
static volatile int mouse_buttons = 0;
static volatile int mouse_updated = 0;

static int bound_x = 1024;
static int bound_y = 768;

// PS/2 mouse packet state machine
static volatile int mouse_cycle = 0;
static volatile uint8_t mouse_packet[4];

// Scroll state
static volatile int mouse_scroll_delta = 0;
static int mouse_intellimouse = 0;

// Press-edge latch: set in the IRQ when the left button goes 0->1, holding
// the pointer position as it was BEFORE the press packet's movement was
// applied. Survives until the GUI/WM consumes it, so even a tap shorter
// than one frame is not lost, and clicks land where the press started
// instead of where the press-slide pushed the cursor.
static volatile int click_latched = 0;
static volatile int click_lx = 0, click_ly = 0;
static volatile int prev_btn_state = 0;

// ── I/O delay for hardware compatibility ────────────────────
static void io_delay(void) {
    // Short delay using port 0x80 (POST diagnostic port)
    // This ensures the PS/2 controller has time to process commands
    // Critical for compatibility with GNOME Boxes (TCG mode)
    port_byte_in(0x80);
    port_byte_in(0x80);
    port_byte_in(0x80);
    port_byte_in(0x80);
}

// ── PS/2 controller helpers ─────────────────────────────────
static void mouse_wait_input(void) {
    int timeout = 100000;
    while (timeout--) {
        if (!(port_byte_in(0x64) & 0x02)) return;
        io_delay();
    }
}

static void mouse_wait_output(void) {
    int timeout = 100000;
    while (timeout--) {
        if (port_byte_in(0x64) & 0x01) return;
        io_delay();
    }
}

static uint8_t mouse_read(void) {
    mouse_wait_output();
    return port_byte_in(0x60);
}

static void mouse_write(uint8_t data) {
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);  // Tell controller: next byte goes to mouse
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, data);
    io_delay();
    
    // Read and discard ACK (0xFA) from the mouse
    mouse_read();
}

// ── Initialization ──────────────────────────────────────────
void init_mouse(void) {
    uint8_t init_status = port_byte_in(0x64);
    if (init_status == 0xFF) {
        // No PS/2 controller present on this system
        return;
    }

    // 1. Flush controller buffer thoroughly
    int flush_count = 0;
    while ((port_byte_in(0x64) & 1) && flush_count < 100) {
        port_byte_in(0x60);
        io_delay();
        flush_count++;
    }

    // 2. Disable both devices first for clean init
    mouse_wait_input();
    port_byte_out(0x64, 0xAD); // Disable keyboard
    io_delay();
    mouse_wait_input();
    port_byte_out(0x64, 0xA7); // Disable mouse
    io_delay();
    
    // Flush again
    flush_count = 0;
    while ((port_byte_in(0x64) & 1) && flush_count < 100) { port_byte_in(0x60); io_delay(); flush_count++; }

    // 3. Read controller configuration byte
    mouse_wait_input();
    port_byte_out(0x64, 0x20);
    io_delay();
    mouse_wait_output();
    uint8_t config = port_byte_in(0x60);

    // Enable IRQ1 (keyboard, bit 0) and IRQ12 (mouse, bit 1)
    config |= 0x03;
    // Enable clocks for both ports (clear disable bits 4 and 5)
    config &= ~0x30;
    // Ensure translation is enabled (bit 6) for compatibility
    config |= 0x40;

    // Write back modified config
    mouse_wait_input();
    port_byte_out(0x64, 0x60);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, config);
    io_delay();

    // 4. Enable both devices
    mouse_wait_input();
    port_byte_out(0x64, 0xAE); // Enable keyboard
    io_delay();
    mouse_wait_input();
    port_byte_out(0x64, 0xA8); // Enable mouse (auxiliary)
    io_delay();

    // 5. Perform interface test (command 0xA9)
    mouse_wait_input();
    port_byte_out(0x64, 0xA9);
    io_delay();
    mouse_wait_output();
    uint8_t test_result = port_byte_in(0x60);
    (void)test_result; // We proceed regardless — some VMs return wrong values

    // 6. Re-enable mouse after test (test may disable it)
    mouse_wait_input();
    port_byte_out(0x64, 0xA8);
    io_delay();
    
    // 7. Reset mouse
    mouse_write(0xFF); // Reset command
    // Mouse should respond with 0xFA (ACK), then 0xAA (self-test pass), then 0x00
    // But we already consumed the ACK in mouse_write, so read the remaining
    mouse_wait_output();
    port_byte_in(0x60); // 0xAA or timeout
    io_delay();
    mouse_wait_output();
    port_byte_in(0x60); // 0x00 or timeout
    io_delay();

    // 8. Set defaults and enable streaming
    mouse_write(0xF6); // Set default values
    mouse_write(0xF4); // Enable data streaming
    
    // 9. Clear any potential leftover bytes
    flush_count = 0;
    while ((port_byte_in(0x64) & 1) && flush_count < 100) {
        port_byte_in(0x60);
        io_delay();
        flush_count++;
    }

    mouse_cycle = 0;

    // Try to enable IntelliMouse scroll-wheel extension (best-effort)
    // The classic magic: 200 Hz, 100 Hz, 80 Hz sample rates, then query the ID.
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xF5); // Disable streaming during rate change
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xF3); // Set sample rate
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xC8); // 200
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xF3); // Set sample rate
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0x64); // 100
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xF3); // Set sample rate
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0x50); // 80
    mouse_wait_output();
    port_byte_in(0x60);
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xF2); // Get ID
    mouse_wait_output();
    port_byte_in(0x60);  // ACK (0xFA)
    /* Wait for the ID byte itself — without this, the read races the
       controller on real hardware and we mis-detect the wheel protocol,
       leaving the touchpad in 4-byte mode while we parse 3-byte packets. */
    mouse_wait_output();
    io_delay();
    {
        unsigned char mid = (unsigned char)port_byte_in(0x60);
        if (mid == 0x03 || mid == 0x04) mouse_intellimouse = 1;
    }
    mouse_wait_input();
    port_byte_out(0x64, 0xD4);
    io_delay();
    mouse_wait_input();
    port_byte_out(0x60, 0xF4); // Enable streaming
    mouse_wait_output();
    port_byte_in(0x60);
    flush_count = 0;
    while ((port_byte_in(0x64) & 1) && flush_count++ < 100) port_byte_in(0x60);

    mouse_load_theme_cfg();   // restore persisted cursor theme (cfg/mouse.cfg)
}

// Mouse sensitivity: 1=low, 2=normal, 3=high
static int mouse_sensitivity = 2;

void mouse_set_sensitivity(int level) {
    if (level < 1) level = 1;
    if (level > 3) level = 3;
    mouse_sensitivity = level;
}

int mouse_get_sensitivity(void) { return mouse_sensitivity; }

// ── IRQ12 Handler (called from interrupt stub) ──────────────
void mouse_handler(void) {
    uint8_t status = port_byte_in(0x64);

    // Verify output buffer is full (bit 0)
    if (!(status & 0x01)) return;

    uint8_t data = port_byte_in(0x60);

    switch (mouse_cycle) {
        case 0:
            // First byte: status — validate it has bit 3 set (always 1 for PS/2)
            if (data & 0x08) {
                mouse_packet[0] = data;
                mouse_cycle = 1;
            }
            // If bit 3 not set, discard and re-sync
            break;

        case 1:
            // Second byte: X movement
            mouse_packet[1] = data;
            mouse_cycle = 2;
            break;

        case 2:
            // Third byte: Y movement
            mouse_packet[2] = data;
            // Check for overflow — discard packet if overflow
            if (mouse_packet[0] & 0xC0) {
                mouse_cycle = 0;
                break;
            }

            // Parse buttons
            mouse_buttons = mouse_packet[0] & 0x07;

            // Parse X movement (signed)
            int dx = (int)mouse_packet[1];
            if (mouse_packet[0] & 0x10) dx |= 0xFFFFFF00;  // Sign extend

            // Parse Y movement (signed, inverted — PS/2 Y is inverted)
            int dy = (int)mouse_packet[2];
            if (mouse_packet[0] & 0x20) dy |= 0xFFFFFF00;  // Sign extend

            // Apply sensitivity multiplier first (so it scales the movement)
            if (mouse_sensitivity == 1) {
                dx = dx / 2;
                dy = dy / 2;
            } else if (mouse_sensitivity == 3) {
                dx = dx + dx / 2;
                dy = dy + dy / 2;
            }

            // Apply movement (Y is inverted for screen coords)
            mouse_x += dx;
            mouse_y -= dy;

            // Clamp to screen bounds
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= bound_x) mouse_x = bound_x - 1;
            if (mouse_y >= bound_y) mouse_y = bound_y - 1;

            /* Latch a fresh left-press at the position the cursor is ACTUALLY
               DRAWN at (after movement + clamp), so a click lands exactly under
               the visible pointer. Latching before movement registered the press
               at a spot the cursor never visually reached, making clicks miss /
               skew to the wrong control. */
            if ((mouse_buttons & 0x01) && !(prev_btn_state & 0x01)) {
                click_latched = 1;
                click_lx = mouse_x;
                click_ly = mouse_y;
            }
            prev_btn_state = mouse_buttons;

            mouse_updated = 1;

            if (mouse_intellimouse) {
                mouse_cycle = 3;
                break;
            }
            mouse_cycle = 0;
            break;

        case 3:
            // Fourth byte: Z/scroll movement (IntelliMouse / IntelliMouse Explorer)
            mouse_packet[3] = data;
            {
                int z = (int)((int8_t)mouse_packet[3]);
                if (z != 0) mouse_scroll_delta += z;
            }
            mouse_cycle = 0;
            break;
    }
}

// ── State accessors ─────────────────────────────────────────
int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }

int mouse_has_update(void) {
    if (mouse_updated) {
        mouse_updated = 0;
        return 1;
    }
    return 0;
}

void mouse_set_bounds(int max_x, int max_y) {
    bound_x = max_x;
    bound_y = max_y;
    // Clamp current position
    if (mouse_x >= bound_x) mouse_x = bound_x - 1;
    if (mouse_y >= bound_y) mouse_y = bound_y - 1;
}

// ── Cursor shape & colour ────────────────────────────────────
typedef struct {
    const char* name;
    const uint8_t* bmp;   // h x w bitmap, values: 0=transparent 1=fill 2=outline 3=highlight
    int w, h;
} cursor_shape_t;

/* Simple channel helpers for deriving outline/highlight from the user colour */
static uint32_t c_lighten(uint32_t c, int f) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r = r + ((255 - r) * f) / 255;
    g = g + ((255 - g) * f) / 255;
    b = b + ((255 - b) * f) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t c_darken(uint32_t c, int f) {
    int r = ((c >> 16) & 0xFF) * f / 255;
    int g = ((c >> 8)  & 0xFF) * f / 255;
    int b = (c & 0xFF) * f / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static int c_lum(uint32_t c) {
    return ((c >> 16) & 0xFF) * 299 + ((c >> 8) & 0xFF) * 587 + (c & 0xFF) * 114;
}

/* Classic arrow (18x12): 1=fill 2=outline 3=highlight */
static const uint8_t arrow_bmp[18][12] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,1,1,1,1,2,0},
    {2,1,1,1,1,1,2,2,2,2,2,0},
    {2,1,1,1,2,1,2,0,0,0,0,0},
    {2,1,1,2,0,2,1,2,0,0,0,0},
    {2,1,2,0,0,2,1,2,0,0,0,0},
    {2,2,0,0,0,0,2,1,2,0,0,0},
    {2,0,0,0,0,0,2,1,2,0,0,0},
    {0,0,0,0,0,0,0,2,0,0,0,0},
};

static const cursor_shape_t g_shapes[MOUSE_CURSOR_COUNT] = {
    { "Classic Pointer", (const uint8_t*)arrow_bmp, 12, 18 },
    { "Circle",          NULL,                      0,  0  },   /* procedural */
};

/* ── Active state + persistence (cfg/mouse.cfg: "type 0xRRGGBB") ── */
static mouse_cursor_t g_cursor = MOUSE_CURSOR_POINTER;
static uint32_t g_cursor_color = 0xFFFFFF;

static void mouse_save_cfg(void) {
    fs_mkdir("cfg");
    fs_delete("cfg/mouse.cfg");
    int fd = fs_create("cfg/mouse.cfg");
    if (fd >= 0) {
        char b[24];
        int n = snprintf(b, sizeof b, "%d 0x%06X", (int)g_cursor, (unsigned)(g_cursor_color & 0xFFFFFF));
        fs_write(fd, b, n); fs_close(fd);
    }
}

/* Tiny hex colour parser ("38BDF8" or "0x38BDF8") — kernel stdlib has no strtoul */
static uint32_t parse_hex(const char* s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint32_t v = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (uint32_t)d;
    }
    return v;
}

/* Load persisted cursor shape + colour at boot (call once from init_mouse).
   Tolerates the legacy index-only format from older builds. */
static void mouse_load_theme_cfg(void) {
    fs_mkdir("cfg");
    int fd = fs_open("cfg/mouse.cfg", 0);
    if (fd >= 0) {
        char b[32]; int n = fs_read(fd, b, sizeof(b) - 1);
        fs_close(fd);
        if (n > 0) {
            b[n] = 0;
            int t = atoi(b);
            const char* sp = strchr(b, ' ');
            if (sp && sp[1]) g_cursor_color = parse_hex(sp + 1) & 0xFFFFFF;
            if (t < 0) t = 0;
            if (t >= MOUSE_CURSOR_COUNT) t = MOUSE_CURSOR_COUNT - 1;
            g_cursor = (mouse_cursor_t)t;
        }
    }
}

void mouse_set_cursor(mouse_cursor_t t) {
    if (t < 0 || t >= MOUSE_CURSOR_COUNT) return;
    g_cursor = t;
    mouse_save_cfg();
}
mouse_cursor_t mouse_get_cursor(void) { return g_cursor; }
const char* mouse_cursor_name(mouse_cursor_t t) {
    if (t < 0 || t >= MOUSE_CURSOR_COUNT) return "?";
    return g_shapes[t].name;
}

void mouse_set_cursor_color(uint32_t rgb) {
    g_cursor_color = rgb & 0xFFFFFF;
    mouse_save_cfg();
}
uint32_t mouse_get_cursor_color(void) { return g_cursor_color; }

/* ── Rendering ──────────────────────────────────────────────── */

/* Coloured arrow: fill = user colour, outline = contrasting, highlight = tint */
static void draw_arrow_at(int mx, int my, uint32_t color, int sc) {
    const cursor_shape_t* th = &g_shapes[MOUSE_CURSOR_POINTER];
    int light = c_lum(color) > 90000;
    uint32_t outline = light ? 0x101014 : c_lighten(color, 150);
    uint32_t hi = light ? 0xFFFFFF : c_lighten(color, 200);

    for (int row = 0; row < th->h; row++) {
        for (int col = 0; col < th->w; col++) {
            uint8_t p = th->bmp[row * th->w + col];
            if (p == 0) continue;
            uint32_t c = (p == 2) ? outline : ((p == 3) ? hi : color);
            int px = mx + col * sc, py = my + row * sc;
            if (sc == 1) gfx_blend_pixel(px, py, c, 255);
            else for (int dy = 0; dy < sc; dy++)
                     for (int dx = 0; dx < sc; dx++)
                         gfx_blend_pixel(px + dx, py + dy, c, 255);
        }
    }
}

/* Google-style filled dot with a small centre hole and a soft fading glow */
static void draw_circle_at(int cx, int cy, uint32_t color, int sc, uint8_t glow_a) {
    int r = 7 * sc;
    int g = 3 * sc;
    int hole = (sc == 1) ? 1 : (2 * sc);
    int r2 = r * r, h2 = hole * hole, go2 = (r + g) * (r + g);
    int denom = go2 - r2;
    if (denom <= 0) denom = 1;

    for (int py = -r - g; py <= r + g; py++) {
        for (int px = -r - g; px <= r + g; px++) {
            int d2 = px * px + py * py;
            if (d2 <= r2) {
                if (d2 <= h2) continue;                  /* centre hole */
                gfx_blend_pixel(cx + px, cy + py, color, 255);
            } else if (d2 <= go2 && glow_a) {            /* glow ring */
                uint8_t a = (uint8_t)((uint32_t)glow_a * (go2 - d2) / denom);
                gfx_blend_pixel(cx + px, cy + py, color, a);
            }
        }
    }
}

void mouse_draw_cursor(void) {
    if (g_cursor == MOUSE_CURSOR_CIRCLE)
        draw_circle_at(mouse_x, mouse_y, g_cursor_color, 1, 26);
    else
        draw_arrow_at(mouse_x, mouse_y, g_cursor_color, 1);
}

/* Draw a shape/colour cursor centered at (cx,cy) at a scale — used by the
   Mouse Settings live preview. glow_alpha drives the circle's pulsing halo. */
void mouse_draw_cursor_preview(int cx, int cy, mouse_cursor_t t,
                               uint32_t color, int scale, uint8_t glow_alpha) {
    if (t < 0 || t >= MOUSE_CURSOR_COUNT) return;
    if (t == MOUSE_CURSOR_CIRCLE) {
        draw_circle_at(cx, cy, color, scale, glow_alpha);
        return;
    }
    const cursor_shape_t* th = &g_shapes[MOUSE_CURSOR_POINTER];
    draw_arrow_at(cx - th->w * scale / 2, cy - th->h * scale / 2, color, scale);
}

int mouse_get_wheel_delta(void) {
    return mouse_scroll_delta;
}

void mouse_clear_wheel_delta(void) {
    mouse_scroll_delta = 0;
}

int mouse_has_click(void) {
    return click_latched;
}

int mouse_get_click_x(void) {
    return click_lx;
}

int mouse_get_click_y(void) {
    return click_ly;
}

int mouse_consume_click(int* x, int* y) {
    if (!click_latched) return 0;
    click_latched = 0;
    if (x) *x = click_lx;
    if (y) *y = click_ly;
    return 1;
}
