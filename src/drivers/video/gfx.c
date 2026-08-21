/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/gpu.h"
#include <string.h>
int gfx_scale = 1;

// ── Utility Primitives ───────────────────────────────────────

extern uint32_t* gfx_get_back_buffer(void);
extern uint32_t  gfx_get_fb_width(void);
extern uint32_t  gfx_get_fb_height(void);
extern uint32_t  gfx_get_stride(void);
extern uint32_t  gfx_rgb_to_pixel(uint32_t rgb);

// ── Clipping System ──────────────────────────────────────────

static int clip_x0 = 0, clip_y0 = 0, clip_x1 = 20000, clip_y1 = 20000;
#define CLIP_STACK_SIZE 16
static struct { int x0, y0, x1, y1; } clip_stack[CLIP_STACK_SIZE];
static int clip_ptr = 0;

void gfx_reset_clip(void) {
    clip_x0 = 0; clip_y0 = 0;
    clip_x1 = gfx_get_fb_width();
    clip_y1 = gfx_get_fb_height();
    clip_ptr = 0;
}

void gfx_set_clip(int x, int y, int w, int h) {
    clip_x0 = x; clip_y0 = y;
    clip_x1 = x + w; clip_y1 = y + h;
}

void gfx_push_clip(int x, int y, int w, int h) {
    if (clip_ptr < CLIP_STACK_SIZE) {
        clip_stack[clip_ptr].x0 = clip_x0;
        clip_stack[clip_ptr].y0 = clip_y0;
        clip_stack[clip_ptr].x1 = clip_x1;
        clip_stack[clip_ptr].y1 = clip_y1;
        clip_ptr++;
    }
    int nx0 = x, ny0 = y, nx1 = x + w, ny1 = y + h;
    if (nx0 < clip_x0) nx0 = clip_x0;
    if (ny0 < clip_y0) ny0 = clip_y0;
    if (nx1 > clip_x1) nx1 = clip_x1;
    if (ny1 > clip_y1) ny1 = clip_y1;
    clip_x0 = nx0; clip_y0 = ny0; clip_x1 = nx1; clip_y1 = ny1;
}

void gfx_pop_clip(void) {
    if (clip_ptr > 0) {
        clip_ptr--;
        clip_x0 = clip_stack[clip_ptr].x0;
        clip_y0 = clip_stack[clip_ptr].y0;
        clip_x1 = clip_stack[clip_ptr].x1;
        clip_y1 = clip_stack[clip_ptr].y1;
    }
}

// ── Utility ─────────────────────────────────────────────────

static int64_t gfx_isqrt64(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = n;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

uint32_t gfx_lerp_color(uint32_t c1, uint32_t c2, int t, int max) {
    if (max <= 0) return c1;
    if (t <= 0) return c1;
    if (t >= max) return c2;
    int r = RGB_R(c1) + ((RGB_R(c2) - RGB_R(c1)) * t) / max;
    int g = RGB_G(c1) + ((RGB_G(c2) - RGB_G(c1)) * t) / max;
    int b = RGB_B(c1) + ((RGB_B(c2) - RGB_B(c1)) * t) / max;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return RGB(r, g, b);
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t pixel = gfx_rgb_to_pixel(rgb);
    
    int x0 = x; if (x0 < clip_x0) x0 = clip_x0;
    int y0 = y; if (y0 < clip_y0) y0 = clip_y0;
    int x1 = x + w; if (x1 > clip_x1) x1 = clip_x1;
    if (x1 > (int)fw) x1 = (int)fw;
    int y1 = y + h; if (y1 > clip_y1) y1 = clip_y1;
    if (y1 > (int)fh) y1 = (int)fh;
    
    if (x1 <= x0 || y1 <= y0) return;
    
    for (int py = y0; py < y1; py++) {
        uint32_t* row = bb + py * stride + x0;
        for (int px = x0; px < x1; px++) {
            *row++ = pixel;
        }
    }
}

void gfx_draw_bevel_rect(int x, int y, int w, int h, uint32_t bg, int sunken) {
    uint32_t highlight = gfx_lighten(bg, 40);
    uint32_t shadow = gfx_darken(bg, 40);
    uint32_t deep_shadow = gfx_darken(bg, 80);
    
    gfx_fill_rect(x, y, w, h, bg);
    
    if (sunken) {
        gfx_draw_hline(x, y, w, shadow);
        gfx_draw_vline(x, y, h, shadow);
        gfx_draw_hline(x+1, y+1, w-2, deep_shadow);
        gfx_draw_vline(x+1, y+1, h-2, deep_shadow);
        gfx_draw_hline(x, y+h-1, w, highlight);
        gfx_draw_vline(x+w-1, y, h, highlight);
    } else {
        gfx_draw_hline(x, y, w, highlight);
        gfx_draw_vline(x, y, h, highlight);
        gfx_draw_hline(x+1, y+h-2, w-2, shadow);
        gfx_draw_vline(x+w-2, y+1, h-2, shadow);
        gfx_draw_hline(x, y+h-1, w, deep_shadow);
        gfx_draw_vline(x+w-1, y, h, deep_shadow);
    }
}

void gfx_draw_pill(int x, int y, const char* label, uint32_t bg_rgb, uint32_t fg_rgb) {
    int len = gfx_strlen(label);
    int pw = len * 8 + 16;
    int ph = 20;
    gfx_fill_rect(x, y, pw, ph, bg_rgb);
    gfx_draw_string_transparent(x + 8, y + 2, label, fg_rgb);
}

void gfx_draw_progress_bar(int x, int y, int w, int h, int percent, uint32_t fill_rgb, uint32_t bg_rgb) {
    gfx_fill_rect(x, y, w, h, bg_rgb);
    if (percent > 0) {
        int fw = (w * percent) / 100;
        if (fw > w) fw = w;
        gfx_fill_rect(x, y, fw, h, fill_rgb);
    }
    gfx_draw_rect_outline(x, y, w, h, 1, gfx_darken(bg_rgb, 40));
}

void gfx_draw_separator(int x, int y, int w, uint32_t rgb) {
    gfx_draw_hline(x, y, w, rgb);
}

void gfx_draw_switch(int x, int y, int w, int h, int state) {
    uint32_t bg = state ? 0x4CAF50 : 0x9E9E9E;
    gfx_draw_bevel_rect(x, y, w, h, bg, 1);
    int thumb_w = h - 8;
    int thumb_x = state ? (x + w - thumb_w - 4) : (x + 4);
    gfx_draw_bevel_rect(thumb_x, y + 4, thumb_w, thumb_w, 0xFFFFFF, 0);
}

void gfx_blend_pixel(int x, int y, uint32_t rgb, uint8_t alpha) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    if (x < 0 || (uint32_t)x >= fw || y < 0 || (uint32_t)y >= fh) return;
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t existing = bb[y * stride + x];
    int r = (RGB_R(rgb) * alpha + RGB_R(existing) * (255 - alpha)) / 255;
    int g = (RGB_G(rgb) * alpha + RGB_G(existing) * (255 - alpha)) / 255;
    int b = (RGB_B(rgb) * alpha + RGB_B(existing) * (255 - alpha)) / 255;
    bb[y * stride + x] = gfx_rgb_to_pixel(RGB(r, g, b));
}

void gfx_draw_rect_outline(int x, int y, int w, int h, int thickness, uint32_t rgb) {
    gfx_fill_rect(x, y, w, thickness, rgb);
    gfx_fill_rect(x, y + h - thickness, w, thickness, rgb);
    gfx_fill_rect(x, y + thickness, thickness, h - 2 * thickness, rgb);
    gfx_fill_rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, rgb);
}

void gfx_draw_shadow(int x, int y, int w, int h, int size) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    /* honor the active clip rect so shadows never bleed outside a clipped
       region (e.g. a clipped scroll list) */
    int cx0 = clip_x0 < 0 ? 0 : clip_x0;
    int cy0 = clip_y0 < 0 ? 0 : clip_y0;
    int cx1 = clip_x1 > (int)fw ? (int)fw : clip_x1;
    int cy1 = clip_y1 > (int)fh ? (int)fh : clip_y1;
    for (int s = size; s > 0; s--) {
        uint32_t alpha = (40 * (size - s + 1)) / size;
        int y0 = y + s; if (y0 < cy0) y0 = cy0;
        int y1 = y + s + h; if (y1 < cy0) y1 = cy0; if (y1 > cy1) y1 = cy1;
        int x0 = x + s; if (x0 < cx0) x0 = cx0;
        int x1 = x + s + w; if (x1 < cx0) x1 = cx0; if (x1 > cx1) x1 = cx1;
        if (x1 <= x0 || y1 <= y0) continue;
        for (int py = y0; py < y1; py++) {
            for (int px = x0; px < x1; px++) {
                // Optimize: skip the inner region that will be completely overwritten by the solid window
                if (px >= x && px < x + w && py >= y && py < y + h) {
                    px = x + w - 1;
                    continue;
                }
                uint32_t existing = bb[py * stride + px];
                int r = (RGB_R(existing) * (255 - alpha)) / 255;
                int g = (RGB_G(existing) * (255 - alpha)) / 255;
                int b = (RGB_B(existing) * (255 - alpha)) / 255;
                bb[py * stride + px] = gfx_rgb_to_pixel(RGB(r, g, b));
            }
        }
    }
}

void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha) {
    if (alpha == 255) {
        gfx_fill_rect(x, y, w, h, rgb);
        return;
    }

    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    
    int x0 = x; if (x0 < 0) x0 = 0;
    int y0 = y; if (y0 < 0) y0 = 0;
    int x1 = x + w; if (x1 > (int)fw) x1 = (int)fw;
    int y1 = y + h; if (y1 > (int)fh) y1 = (int)fh;
    
    uint32_t inv_a = 256 - alpha;
    uint32_t sr = (rgb >> 16) & 0xFF;
    uint32_t sg = (rgb >> 8) & 0xFF;
    uint32_t sb = rgb & 0xFF;

    for (int py = y0; py < y1; py++) {
        uint32_t* row = bb + py * stride;
        for (int px = x0; px < x1; px++) {
            uint32_t existing = row[px];
            uint32_t r = (sr * alpha + ((existing >> 16) & 0xFF) * inv_a) >> 8;
            uint32_t g = (sg * alpha + ((existing >> 8) & 0xFF) * inv_a) >> 8;
            uint32_t b = (sb * alpha + (existing & 0xFF) * inv_a) >> 8;
            row[px] = (r << 16) | (g << 8) | b;
        }
    }
}

void gfx_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb) {
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t pixel = gfx_rgb_to_pixel(rgb);

    int minx = x0, maxx = x0, miny = y0, maxy = y0;
    if (x1 < minx) minx = x1; if (x1 > maxx) maxx = x1;
    if (x2 < minx) minx = x2; if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1; if (y1 > maxy) maxy = y1;
    if (y2 < miny) miny = y2; if (y2 > maxy) maxy = y2;
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if ((uint32_t)maxx >= fw) maxx = (int)fw - 1;
    if ((uint32_t)maxy >= fh) maxy = (int)fh - 1;
    if (maxx < minx || maxy < miny) return;

    int dx12 = x1 - x0, dy12 = y1 - y0;
    int dx20 = x2 - x0, dy20 = y2 - y0;
    int area2 = dx12 * dy20 - dy12 * dx20;
    if (area2 == 0) return;

    for (int py = miny; py <= maxy; py++) {
        for (int px = minx; px <= maxx; px++) {
            int e0 = (x1 - x2) * (py - y2) - (y1 - y2) * (px - x2);
            int e1 = (x2 - x0) * (py - y0) - (y2 - y0) * (px - x0);
            int e2 = (x0 - x1) * (py - y1) - (y0 - y1) * (px - x1);
            if (area2 > 0) {
                if (e0 >= 0 && e1 >= 0 && e2 >= 0)
                    bb[py * stride + px] = pixel;
            } else {
                if (e0 <= 0 && e1 <= 0 && e2 <= 0)
                    bb[py * stride + px] = pixel;
            }
        }
    }
}

void gfx_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb) {
    gfx_draw_line(x0, y0, x1, y1, rgb);
    gfx_draw_line(x1, y1, x2, y2, rgb);
    gfx_draw_line(x2, y2, x0, y0, rgb);
}

void gfx_fill_rect_rounded(int x, int y, int w, int h, int radius, uint32_t rgb) {
    if (radius < 1 || w <= radius * 2 || h <= radius * 2) {
        gfx_fill_rect(x, y, w, h, rgb);
        return;
    }
    uint32_t pixel = gfx_rgb_to_pixel(rgb);
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();

    // Clip
    int x0 = x; if (x0 < 0) x0 = 0;
    int y0 = y; if (y0 < 0) y0 = 0;
    int x1 = x + w; if (x1 > (int)fw) x1 = (int)fw;
    int y1 = y + h; if (y1 > (int)fh) y1 = (int)fh;
    if (x1 <= x0 || y1 <= y0) return;

    // 2x2 supersampled anti-aliased quarter-circle corners.
    // coords scaled by 4; a pixel is on if its 4 sample points fall inside.
    int r4 = radius * 4;
    long r2 = (long)r4 * r4;
    int cx_l = (x + radius) * 4, cx_r = (x + w - radius) * 4;
    int cy_t = (y + radius) * 4, cy_b = (y + h - radius) * 4;
    int ss_x[4] = {1, 3, 1, 3};
    int ss_y[4] = {1, 1, 3, 3};

    for (int py = y0; py < y1; py++) {
        int rel_y = py - y;
        int in_top    = (rel_y < radius);
        int in_bottom = (rel_y >= h - radius);
        uint32_t *row = &bb[py * stride];
        for (int px = x0; px < x1; px++) {
            int rel_x = px - x;

            if (in_top) {
                if (rel_x < radius)      { /* top-left corner */ }
                else if (rel_x >= w - radius) { /* top-right corner */ }
                else { row[px] = pixel; continue; }
            } else if (in_bottom) {
                if (rel_x < radius)      { /* bottom-left corner */ }
                else if (rel_x >= w - radius) { /* bottom-right corner */ }
                else { row[px] = pixel; continue; }
            } else {
                row[px] = pixel;
                continue;
            }

            // determine which corner quarter-circle this pixel belongs to
            int ccx, ccy;
            if (in_top && rel_x < radius)            { ccx = cx_l; ccy = cy_t; }
            else if (in_top && rel_x >= w - radius)   { ccx = cx_r; ccy = cy_t; }
            else if (in_bottom && rel_x < radius)     { ccx = cx_l; ccy = cy_b; }
            else                                       { ccx = cx_r; ccy = cy_b; }

            int px4 = px * 4, py4 = py * 4;
            int hits = 0;
            for (int s = 0; s < 4; s++) {
                int ddx = px4 + ss_x[s] - ccx;
                int ddy = py4 + ss_y[s] - ccy;
                if ((long)ddx * ddx + (long)ddy * ddy <= r2) hits++;
            }
            if (hits == 4) {
                row[px] = pixel;
            } else if (hits > 0) {
                // partial pixel: alpha-blend over the existing background
                gfx_blend_pixel(px, py, rgb, (uint8_t)(hits * 64));
            }
            // hits == 0: corner cutout, leave background untouched
        }
    }
}

void gfx_fill_gradient_v(int x, int y, int w, int h, uint32_t rgb_top, uint32_t rgb_bottom) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    int y0 = y; if (y0 < 0) y0 = 0;
    int y1 = y + h; if (y1 < 0) y1 = 0; if (y1 > (int)fh) y1 = (int)fh;
    int x0 = x; if (x0 < 0) x0 = 0;
    int x1 = x + w; if (x1 < 0) x1 = 0; if (x1 > (int)fw) x1 = (int)fw;
    for (int py = y0; py < y1; py++) {
        uint32_t color = gfx_lerp_color(rgb_top, rgb_bottom, py - y, h - 1);
        uint32_t pixel = gfx_rgb_to_pixel(color);
        for (int px = x0; px < x1; px++) {
            bb[py * stride + px] = pixel;
        }
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint32_t rgb) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t pixel = gfx_rgb_to_pixel(rgb);
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx;
            if (px < 0 || (uint32_t)px >= fw) continue;
            if (dx * dx + dy * dy <= r * r) {
                bb[py * stride + px] = pixel;
            }
        }
    }
}

void gfx_draw_circle(int cx, int cy, int r, uint32_t rgb) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t pixel = gfx_rgb_to_pixel(rgb);
    
    int x = r, y = 0;
    int err = 0;
    
    while (x >= y) {
        int pts[8][2] = {
            {cx + x, cy + y}, {cx + y, cy + x}, {cx - y, cy + x}, {cx - x, cy + y},
            {cx - x, cy - y}, {cx - y, cy - x}, {cx + y, cy - x}, {cx + x, cy - y}
        };
        for (int i = 0; i < 8; i++) {
            int px = pts[i][0], py = pts[i][1];
            if (px >= 0 && (uint32_t)px < fw && py >= 0 && (uint32_t)py < fh) {
                bb[py * stride + px] = pixel;
            }
        }
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void gfx_draw_char(int px, int py, char c, uint32_t fg_rgb, uint32_t bg_rgb) {
    int s = gfx_scale;
    gfx_fill_rect(px, py, 8 * s, 16 * s, bg_rgb);
    gfx_draw_char_transparent(px, py, c, fg_rgb);
}

void gfx_draw_string(int px, int py, const char* str, uint32_t fg_rgb, uint32_t bg_rgb) {
    while (*str) {
        gfx_draw_char(px, py, *str, fg_rgb, bg_rgb);
        px += 8 * gfx_scale;
        str++;
    }
}

void gfx_draw_char_transparent(int x, int y, char c, uint32_t rgb) {
    char s[2] = { c, 0 };
    gfx_draw_string_transparent(x, y, s, rgb);
}

void gfx_draw_string_transparent(int x, int y, const char* str, uint32_t rgb) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t pixel = gfx_rgb_to_pixel(rgb);
    int s = gfx_scale;
    extern const uint8_t font8x16[];

    int clip_x_min = clip_x0;
    int clip_y_min = clip_y0;
    int clip_x_max = clip_x1;
    int clip_y_max = clip_y1;

    while (*str) {
        const uint8_t* glyph = &font8x16[(uint8_t)(*str) * 16];
        for (int gy = 0; gy < 16; gy++) {
            int glyph_y = y + gy * s;
            if (glyph_y + s <= clip_y_min || glyph_y >= clip_y_max) continue;
            for (int sy = 0; sy < s; sy++) {
                int py = glyph_y + sy;
                if (py < clip_y_min || py >= clip_y_max) continue;
                uint8_t row = glyph[gy];
                for (int gx = 0; gx < 8; gx++) {
                    if (!(row & (0x80 >> gx))) continue;
                    int glyph_x = x + gx * s;
                    if (glyph_x + s <= clip_x_min || glyph_x >= clip_x_max) continue;
                    for (int sx = 0; sx < s; sx++) {
                        int px = glyph_x + sx;
                        if (px < clip_x_min || px >= clip_x_max) continue;
                        bb[py * stride + px] = pixel;
                    }
                }
            }
        }
        x += 8 * s;
        str++;
    }
}

/* Crisp scaled text for the calculator result. Each source font pixel is
 * expanded into an aligned scale*scale block drawn in fully-opaque fg
 * (no alpha blend against the dark panel — blending was what made the
 * previous version look washed-out and blurry). Integer-snapped so stems
 * stay sharp instead of smeared. `bg` is ignored (kept for call-site
 * compatibility). */
void gfx_draw_string_scaled_aa(int x, int y, const char* str, uint32_t fg, uint32_t bg, int scale) {
    if (scale <= 1) { gfx_draw_string(x, y, str, fg, bg); return; }
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    extern const uint8_t font8x16[];
    (void)bg;

    int cx0 = clip_x0 < 0 ? 0 : clip_x0;
    int cy0 = clip_y0 < 0 ? 0 : clip_y0;
    int cx1 = clip_x1 > (int)fw ? (int)fw : clip_x1;
    int cy1 = clip_y1 > (int)fh ? (int)fh : clip_y1;

    int char_w = 8, char_h = 16;
    int total_w = (int)strlen(str) * char_w * scale;
    if (x + total_w < cx0 || x >= cx1) return;

    uint32_t px = gfx_rgb_to_pixel(fg);
    /* When upscaling a thin 8x16 glyph, thicken stems by one output pixel so
     * large digits read as solid rather than skeletal. */
    int thick = (scale >= 4) ? 1 : 0;

    for (int i = 0; str[i]; i++) {
        const uint8_t* glyph = &font8x16[(uint8_t)str[i] * 16];
        int ox = x + i * char_w * scale;
        if (ox + char_w * scale < cx0 || ox >= cx1) continue;
        for (int gy = 0; gy < char_h; gy++) {
            uint8_t row = glyph[gy];
            if (!row) continue;
            int py = y + gy * scale;
            if (py >= cy1 || py + scale <= cy0) continue;
            for (int gx = 0; gx < char_w; gx++) {
                if (!(row & (0x80 >> gx))) continue;
                int bx = ox + gx * scale;
                int ex = bx + scale;
                if (thick) { bx -= 1; ex += 1; }
                if (bx < cx0) bx = cx0;
                if (ex > cx1) ex = cx1;
                if (bx >= ex) continue;
                for (int yy = py; yy < py + scale && yy < cy1; yy++) {
                    if (yy < cy0) continue;
                    for (int xx = bx; xx < ex && xx < cx1; xx++) {
                        if (xx < cx0) continue;
                        bb[yy * stride + xx] = px;
                    }
                }
            }
        }
    }
}

void gfx_draw_hline(int x, int y, int w, uint32_t rgb) { gfx_fill_rect(x, y, w, 1, rgb); }
void gfx_draw_vline(int x, int y, int h, uint32_t rgb) { gfx_fill_rect(x, y, 1, h, rgb); }

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t pixel = gfx_rgb_to_pixel(rgb);

    /* Effective clip = intersection of clip region and framebuffer */
    int cx0 = clip_x0 < 0 ? 0 : clip_x0;
    int cy0 = clip_y0 < 0 ? 0 : clip_y0;
    int cx1 = clip_x1 > (int)fw ? (int)fw : clip_x1;
    int cy1 = clip_y1 > (int)fh ? (int)fh : clip_y1;

    int dx = (x1 > x0 ? x1 - x0 : x0 - x1), sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        if (x0 >= cx0 && x0 < cx1 && y0 >= cy0 && y0 < cy1) {
            bb[y0 * stride + x0] = pixel;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t rgb) {
    gfx_draw_line(x0, y0, x1, y1, rgb);
}

void gfx_draw_window(int x, int y, int w, int h, const char* title, uint32_t accent_color) {
    gfx_draw_window_custom(x, y, w, h, title, accent_color, 0);
}

void gfx_draw_window_custom(int x, int y, int w, int h, const char* title, uint32_t accent_color, int is_square) {
    gfx_draw_shadow(x, y, w, h, 8);
    gfx_fill_rect(x, y, w, h, CLR_BG_DARK);

    int title_h = 28;
    gfx_fill_rect(x, y, w, title_h, CLR_WINDOW_TITLE);
    
    gfx_draw_hline(x, y + title_h - 1, w, CLR_BORDER);
    gfx_draw_string_transparent(x + 32, y + (title_h - 16) / 2, title, CLR_TEXT_PRIMARY);
    
    // Premium window controls
    gfx_fill_circle(x + 14, y + title_h / 2, 6, 0xF85149); // Close
}

void gfx_draw_rect_rounded_outline(int x, int y, int w, int h, int r, int t, uint32_t rgb) {
    if (r < 1 || w <= r * 2 || h <= r * 2) { gfx_draw_rect_outline(x, y, w, h, t, rgb); return; }
    uint32_t pixel = gfx_rgb_to_pixel(rgb);
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();

    // Clip
    int x0 = x;                 if (x0 < 0) x0 = 0;
    int y0 = y;                 if (y0 < 0) y0 = 0;
    int x1 = x + w;             if (x1 > (int)fw) x1 = (int)fw;
    int y1 = y + h;             if (y1 > (int)fh) y1 = (int)fh;
    if (x1 <= x0 || y1 <= y0) return;

    // Crisp straight edges (thickness t) for the non-corner columns/rows.
    // Top / bottom straight edges stop at x+r .. x+w-r (corners handled below).
    for (int ty = 0; ty < t; ty++) {
        int py_top = y + ty;
        int py_bot = y + h - 1 - ty;
        for (int px = x + r; px < x + w - r; px++) {
            if (px < x0 || px >= x1) continue;
            if (py_top >= y0 && py_top < y1) bb[py_top * stride + px] = pixel;
            if (py_bot >= y0 && py_bot < y1) bb[py_bot * stride + px] = pixel;
        }
    }
    // Left / right straight edges stop at y+r .. y+h-r.
    for (int tx = 0; tx < t; tx++) {
        int px_l = x + tx;
        int px_r = x + w - 1 - tx;
        for (int py = y + r; py < y + h - r; py++) {
            if (py < y0 || py >= y1) continue;
            if (px_l >= x0 && px_l < x1) bb[py * stride + px_l] = pixel;
            if (px_r >= x0 && px_r < x1) bb[py * stride + px_r] = pixel;
        }
    }

    // 2x2 supersampled anti-aliased quarter-circle corners.
    int r4  = r * 4;
    int rin = (r - t) * 4;              // inner radius of the stroke ring
    long r2_out = (long)r4 * r4;
    long r2_in  = (long)rin * rin;
    int cx_l = (x + r) * 4, cx_r = (x + w - r) * 4;
    int cy_t = (y + r) * 4, cy_b = (y + h - r) * 4;
    int ss_x[4] = {1, 3, 1, 3};
    int ss_y[4] = {1, 1, 3, 3};
    int cxs[4] = {cx_l, cx_r, cx_l, cx_r};
    int cys[4] = {cy_t, cy_t, cy_b, cy_b};
    int oxs[4] = {x,    x + w - r, x,    x + w - r};
    int oys[4] = {y,    y,          y + h - r, y + h - r};
    for (int c = 0; c < 4; c++) {
        int ox = oxs[c], oy = oys[c];
        int ccx = cxs[c], ccy = cys[c];
        for (int py = oy; py < oy + r; py++) {
            if (py < y0 || py >= y1) continue;
            int py4 = py * 4;
            for (int px = ox; px < ox + r; px++) {
                if (px < x0 || px >= x1) continue;
                int px4 = px * 4;
                int hits = 0;
                for (int s = 0; s < 4; s++) {
                    int ddx = px4 + ss_x[s] - ccx;
                    int ddy = py4 + ss_y[s] - ccy;
                    long dd2 = (long)ddx * ddx + (long)ddy * ddy;
                    if (dd2 <= r2_out && dd2 > r2_in) hits++;
                }
                if (hits == 4) {
                    bb[py * stride + px] = pixel;
                } else if (hits > 0) {
                    gfx_blend_pixel(px, py, rgb, (uint8_t)(hits * 64));
                }
            }
        }
    }
}

void gfx_draw_button(int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg, int hover) {
    if (bg == 0) return;
    uint32_t final_bg = hover ? gfx_lighten(bg, 20) : bg;
    gfx_draw_bevel_rect(x, y, w, h, final_bg, hover);
    int len = gfx_strlen(label);
    gfx_draw_string_transparent(x + (w - len * 8) / 2, y + (h - 16) / 2, label, fg);
}

void gfx_fill_gradient_h(int x, int y, int w, int h, uint32_t rgb_left, uint32_t rgb_right) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    int y0 = y; if (y0 < 0) y0 = 0;
    int y1 = y + h; if (y1 < 0) y1 = 0; if (y1 > (int)fh) y1 = (int)fh;
    int x0 = x; if (x0 < 0) x0 = 0;
    int x1 = x + w; if (x1 < 0) x1 = 0; if (x1 > (int)fw) x1 = (int)fw;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            uint32_t color = gfx_lerp_color(rgb_left, rgb_right, px - x, w - 1);
            bb[py * stride + px] = gfx_rgb_to_pixel(color);
        }
    }
}

void gfx_fill_gradient_v_rounded(int x, int y, int w, int h, int r, uint32_t c1, uint32_t c2) {
    gfx_fill_gradient_v(x, y, w, h, c1, c2);
}

uint32_t gfx_darken(uint32_t rgb, int amount) {
    int r = RGB_R(rgb) - amount; if (r < 0) r = 0;
    int g = RGB_G(rgb) - amount; if (g < 0) g = 0;
    int b = RGB_B(rgb) - amount; if (b < 0) b = 0;
    return RGB(r, g, b);
}

uint32_t gfx_lighten(uint32_t rgb, int amount) {
    int r = RGB_R(rgb) + amount; if (r > 255) r = 255;
    int g = RGB_G(rgb) + amount; if (g > 255) g = 255;
    int b = RGB_B(rgb) + amount; if (b > 255) b = 255;
    return RGB(r, g, b);
}

int gfx_strlen(const char* s) { int l = 0; while (s[l]) l++; return l; }

void gfx_draw_rgb_bitmap(int x, int y, int w, int h, const uint8_t* rgb_data) {
    if (!rgb_data) return;
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fw) continue;
            int idx = (row * w + col) * 3;
            bb[py * stride + px] = gfx_rgb_to_pixel(RGB(rgb_data[idx], rgb_data[idx + 1], rgb_data[idx + 2]));
        }
    }
}

void gfx_draw_rgb_bitmap_scaled(int x, int y, int dw, int dh, const uint8_t* rgb_data, int sw, int sh) {
    if (!rgb_data || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    /* Respect clip region (intersection with framebuffer) */
    int cx0 = clip_x0 < 0 ? 0 : clip_x0;
    int cy0 = clip_y0 < 0 ? 0 : clip_y0;
    int cx1 = clip_x1 > (int)fw ? (int)fw : clip_x1;
    int cy1 = clip_y1 > (int)fh ? (int)fh : clip_y1;
    for (int row = 0; row < dh; row++) {
        int py = y + row;
        if (py < cy0 || py >= cy1 || py < 0 || (uint32_t)py >= fh) continue;
        int sy = (row * sh) / dh;
        for (int col = 0; col < dw; col++) {
            int px = x + col;
            if (px < cx0 || px >= cx1 || px < 0 || (uint32_t)px >= fw) continue;
            int sx = (col * sw) / dw;
            int idx = (sy * sw + sx) * 3;
            bb[py * stride + px] = gfx_rgb_to_pixel(RGB(rgb_data[idx], rgb_data[idx + 1], rgb_data[idx + 2]));
        }
    }
}

/* Color-keyed RGB bitmap blit — pixels matching GFX_SPRITE_KEY (0xFF00FF)
   are skipped so the icon has a transparent background on any theme surface.
   Used for real image icons (driver types, etc.). */
void gfx_draw_rgb_bitmap_keyed(int x, int y, int dw, int dh,
                               const uint8_t* rgb_data, int sw, int sh) {
    if (!rgb_data || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    int cx0 = clip_x0 < 0 ? 0 : clip_x0;
    int cy0 = clip_y0 < 0 ? 0 : clip_y0;
    int cx1 = clip_x1 > (int)fw ? (int)fw : clip_x1;
    int cy1 = clip_y1 > (int)fh ? (int)fh : clip_y1;
    for (int row = 0; row < dh; row++) {
        int py = y + row;
        if (py < cy0 || py >= cy1 || py < 0 || (uint32_t)py >= fh) continue;
        int sy = (row * sh) / dh;
        for (int col = 0; col < dw; col++) {
            int px = x + col;
            if (px < cx0 || px >= cx1 || px < 0 || (uint32_t)px >= fw) continue;
            int sx = (col * sw) / dw;
            int idx = (sy * sw + sx) * 3;
            uint32_t r = rgb_data[idx], g = rgb_data[idx + 1], b = rgb_data[idx + 2];
            if ((r == 0xFF) && (g == 0x00) && (b == 0xFF)) continue; /* key */
            bb[py * stride + px] = gfx_rgb_to_pixel(RGB(r, g, b));
        }
    }
}

static void gfx_blit_sprite_pixel(int px, int py, uint32_t rgb) {
    if (rgb == GFX_SPRITE_KEY) return;
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    /* Respect clip region */
    if (px < clip_x0 || px >= clip_x1 || py < clip_y0 || py >= clip_y1) return;
    if (px < 0 || (uint32_t)px >= fw || py < 0 || (uint32_t)py >= fh) return;
    gfx_get_back_buffer()[py * gfx_get_stride() + px] = gfx_rgb_to_pixel(rgb);
}

void gfx_draw_sprite(int x, int y, const uint32_t* src, int sw, int sh) {
    if (!src) return;
    for (int row = 0; row < sh; row++)
        for (int col = 0; col < sw; col++)
            gfx_blit_sprite_pixel(x + col, y + row, src[row * sw + col]);
}

/* Bilinear-filtered upscale — smooth Mandelbrot / image scaling */
void gfx_draw_sprite_scaled(int x, int y, int dw, int dh, const uint32_t* src, int sw, int sh) {
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    /* Clip bounds */
    int cx0 = clip_x0 < 0 ? 0 : clip_x0;
    int cy0 = clip_y0 < 0 ? 0 : clip_y0;
    int cx1 = clip_x1 > (int)fw ? (int)fw : clip_x1;
    int cy1 = clip_y1 > (int)fh ? (int)fh : clip_y1;
    for (int row = 0; row < dh; row++) {
        int py = y + row;
        if (py < cy0 || py >= cy1 || py < 0 || (uint32_t)py >= fh) continue;
        /* Fixed-point source Y with bilinear */
        int fy = (row * (sh - 1) * 256) / (dh > 1 ? dh - 1 : 1);
        int sy0 = fy >> 8; if (sy0 >= sh - 1) sy0 = sh - 2;
        int sy1 = sy0 + 1;
        int ty  = fy & 0xFF;
        for (int col = 0; col < dw; col++) {
            int px = x + col;
            if (px < cx0 || px >= cx1 || px < 0 || (uint32_t)px >= fw) continue;
            int fx = (col * (sw - 1) * 256) / (dw > 1 ? dw - 1 : 1);
            int sx0 = fx >> 8; if (sx0 >= sw - 1) sx0 = sw - 2;
            int sx1 = sx0 + 1;
            int tx  = fx & 0xFF;
            /* 4-corner samples */
            uint32_t c00 = src[sy0 * sw + sx0];
            uint32_t c10 = src[sy0 * sw + sx1];
            uint32_t c01 = src[sy1 * sw + sx0];
            uint32_t c11 = src[sy1 * sw + sx1];
            /* Bilinear blend each channel */
            int r = (((int)((c00>>16)&0xFF) * (256-tx) + (int)((c10>>16)&0xFF) * tx) * (256-ty)
                   + ((int)((c01>>16)&0xFF) * (256-tx) + (int)((c11>>16)&0xFF) * tx) * ty) >> 16;
            int g = (((int)((c00>>8)&0xFF)  * (256-tx) + (int)((c10>>8)&0xFF)  * tx) * (256-ty)
                   + ((int)((c01>>8)&0xFF)  * (256-tx) + (int)((c11>>8)&0xFF)  * tx) * ty) >> 16;
            int b = (((int)(c00&0xFF)       * (256-tx) + (int)(c10&0xFF)       * tx) * (256-ty)
                   + ((int)(c01&0xFF)       * (256-tx) + (int)(c11&0xFF)       * tx) * ty) >> 16;
            bb[py * stride + px] = gfx_rgb_to_pixel(((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b);
        }
    }
}

void gfx_draw_image(int x, int y, int w, int h, const uint8_t* d) {
    if (d) gfx_draw_rgb_bitmap(x, y, w, h, d);
}

void gfx_draw_icon_rgba(int x, int y, int size, const uint32_t* rgba_data) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();

    int orig_size = 48;
    int u32_per_row = orig_size / 2;

    for (int row = 0; row < size; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fh) continue;
        
        // Nearest-neighbor scaling
        int orig_row = (row * orig_size) / size;
        const uint32_t* row_data = rgba_data + (orig_row * u32_per_row);
        
        for (int col = 0; col < size; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fw) continue;
            
            int orig_col = (col * orig_size) / size;
            uint32_t pack = row_data[orig_col / 2];
            uint16_t pixel16 = (orig_col % 2 == 0) ? (pack >> 16) : (pack & 0xFFFF);
            
            // Extract RGBA4444
            uint8_t r = (pixel16 >> 12) & 0xF;
            uint8_t g = (pixel16 >> 8) & 0xF;
            uint8_t b = (pixel16 >> 4) & 0xF;
            uint8_t a = pixel16 & 0xF;
            
            if (a == 0) continue;
            
            // Scale to 0-255
            uint8_t R = r * 17;
            uint8_t G = g * 17;
            uint8_t B = b * 17;
            uint8_t A = a * 17;
            
            if (A >= 250) {
                bb[py * stride + px] = gfx_rgb_to_pixel(RGB(R, G, B));
            } else {
                uint32_t existing = bb[py * stride + px];
                int er = (existing >> 16) & 0xFF;
                int eg = (existing >> 8) & 0xFF;
                int eb = existing & 0xFF;
                int nr = (R * A + er * (255 - A)) / 255;
                int ng = (G * A + eg * (255 - A)) / 255;
                int nb = (B * A + eb * (255 - A)) / 255;
                bb[py * stride + px] = gfx_rgb_to_pixel(RGB(nr, ng, nb));
            }
        }
    }
}

void gfx_draw_icon_1bit(int x, int y, const uint8_t* data, int w, int h, uint32_t color) {
    uint32_t fw = gfx_get_fb_width();
    uint32_t fh = gfx_get_fb_height();
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t pixel = gfx_rgb_to_pixel(color);
    int bytes_per_row = (w + 7) / 8;
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fw) continue;
            int byte_idx = row * bytes_per_row + (col / 8);
            int bit = 7 - (col % 8);
            if (data[byte_idx] & (1 << bit)) {
                bb[py * stride + px] = pixel;
            }
        }
    }
}
