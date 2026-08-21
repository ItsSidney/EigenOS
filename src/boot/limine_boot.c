/* limine_boot.c — Limine-style boot screen for EigenOS.
 *
 * A real background image (scaled cover-fit) with a Limine opacity overlay,
 * plus a centered, translucent panel that streams an OpenRC/runit/dinit-style
 * init log (a scrolling list of [ OK ] service lines), and a bottom progress
 * bar. Lives in its own folder, separate from the framebuffer driver.
 *
 * Driven from the kernel via draw_splash_screen() -> limine_boot_render(progress),
 * where progress is the boot stage (0..31) kept in framebuffer.c. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "libs/bmp.h"

/* ---- framebuffer accessors (exported from framebuffer.c) ---- */
extern uint32_t* fb_get_back_buffer(void);
extern uint32_t  fb_rgb_to_pixel(uint32_t rgb);
extern void      fb_unpack_rgb(uint32_t px, uint8_t* r, uint8_t* g, uint8_t* b);
extern void      fb_swap_buffers(void);
extern void      fb_draw_text_t(int x, int y, const char* str, uint32_t fg);
extern uint32_t  get_fb_width(void);
extern uint32_t  get_fb_height(void);
extern int       fb_boot_log_count(void);
extern const char* fb_boot_log_msg(int i);

/* ---- kernel services ---- */
extern void*  kmalloc(size_t n);
extern int    user_module_find(const char* name, const void** data, uint64_t* size);

/* ---- cached, opacity-blended background (framebuffer-packed pixels) ---- */
static uint32_t* g_bg = 0;
static int       g_bg_w = 0, g_bg_h = 0;
static int       g_bg_ready = 0;

/* Limine-style palette */
#define LIM_TINT    0x0A0D14
#define LIM_ACCENT  0x38BDF8
#define LIM_ACCENT2 0x58A6FF
#define LIM_DIM     0x8B949E
#define LIM_GRAY    0x9AA4B2
#define LIM_WHITE   0xF0F6FC
#define LIM_GREEN   0x3FB950
#define LIM_WARN    0xF0883E

static int my_strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }

/* Integer alpha blend of `col` (0xRRGGBB) over an existing packed pixel. */
static void blend_px(uint32_t* dst, uint32_t col, uint8_t a) {
    uint8_t r, g, b;
    fb_unpack_rgb(*dst, &r, &g, &b);
    uint8_t cr = (col >> 16) & 0xFF, cg = (col >> 8) & 0xFF, cb = col & 0xFF;
    r = (uint8_t)(r + ((int)cr - r) * a / 255);
    g = (uint8_t)(g + ((int)cg - g) * a / 255);
    b = (uint8_t)(b + ((int)cb - b) * a / 255);
    *dst = fb_rgb_to_pixel((r << 16) | (g << 8) | b);
}

static void blend_rect(uint32_t* bb, int fw, int fh,
                       int x, int y, int w, int h, uint32_t col, uint8_t a) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= fh) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= fw) continue;
            blend_px(&bb[yy * fw + xx], col, a);
        }
    }
}

/* Decode wp1.bmp and build a cover-fit, opacity-blended full-screen cache. */
static void build_background(void) {
    int fw = (int)get_fb_width(), fh = (int)get_fb_height();
    if (fw <= 0 || fh <= 0) return;

    const void* data = 0;
    uint64_t size = 0;
    if (user_module_find("wp1", &data, &size) != 0) return;

    bmp_image_t img; img.pixels = 0;
    if (bmp_decode(data, (int)size, &img) != 1) return;

    uint32_t* buf = (uint32_t*)kmalloc((size_t)fw * (size_t)fh * 4);
    if (!buf) { bmp_free(&img); return; }

    int iw = img.width, ih = img.height;
    long fw_l = fw, fh_l = fh, iw_l = iw, ih_l = ih;
    long num, den;
    if (fw_l * ih_l > fh_l * iw_l) { num = fw_l; den = iw_l; }
    else                          { num = fh_l; den = ih_l; }
    long dw = iw_l * num / den;
    long dh = ih_l * num / den;
    long offx = (fw_l - dw) / 2;
    long offy = (fh_l - dh) / 2;

    const uint8_t op = 82;   /* Limine background-image opacity % */
    uint8_t tr = (LIM_TINT >> 16) & 0xFF, tg = (LIM_TINT >> 8) & 0xFF, tb = LIM_TINT & 0xFF;

    for (int y = 0; y < fh; y++) {
        long sy = (y - offy) * ih_l / dh;
        for (int x = 0; x < fw; x++) {
            long sx = (x - offx) * iw_l / dw;
            uint8_t r = tr, g = tg, b = tb;
            if (sx >= 0 && sx < iw && sy >= 0 && sy < ih) {
                const uint8_t* p = img.pixels + (sy * iw + sx) * 3;
                r = p[0]; g = p[1]; b = p[2];
            }
            r = (uint8_t)(r * op / 100 + tr * (100 - op) / 100);
            g = (uint8_t)(g * op / 100 + tg * (100 - op) / 100);
            b = (uint8_t)(b * op / 100 + tb * (100 - op) / 100);
            buf[y * fw + x] = fb_rgb_to_pixel((r << 16) | (g << 8) | b);
        }
    }

    bmp_free(&img);
    g_bg = buf; g_bg_w = fw; g_bg_h = fh; g_bg_ready = 1;
}

/* OpenRC/runit/dinit-style scrolling init log, inside a translucent panel. */
static void draw_init_log(uint32_t* bb, int fw, int fh) {
    int x0 = (int)(fw * 0.07);
    int y0 = (int)(fh * 0.20);
    int x1 = (int)(fw * 0.93);
    int y1 = (int)(fh * 0.72);
    int pw = x1 - x0, ph = y1 - y0;

    /* translucent dark panel for legibility over the image */
    blend_rect(bb, fw, fh, x0, y0, pw, ph, LIM_TINT, 150);
    /* subtle accent frame */
    blend_rect(bb, fw, fh, x0, y0, pw, 2, LIM_ACCENT, 130);
    blend_rect(bb, fw, fh, x0, y0, 2, ph, LIM_ACCENT, 80);
    blend_rect(bb, fw, fh, x1 - 2, y0, 2, ph, LIM_ACCENT, 80);
    blend_rect(bb, fw, fh, x0, y1 - 2, pw, 2, LIM_ACCENT, 130);

    /* header line */
    fb_draw_text_t(x0 + 16, y0 + 8, "EigenOS  -  initializing system", fb_rgb_to_pixel(LIM_ACCENT));

    int lh = 18;
    int header_h = 30;
    int avail = ph - header_h - 8;
    int max_lines = avail / lh;
    int total = fb_boot_log_count();
    int start = (total > max_lines) ? (total - max_lines) : 0;

    int badge_x = x1 - 78;
    int msg_max = (badge_x - (x0 + 16 + 24) - 8) / 8;

    for (int i = start; i < total; i++) {
        int row = i - start;
        int y = y0 + header_h + row * lh;
        const char* m = fb_boot_log_msg(i);

        fb_draw_text_t(x0 + 16, y, " * ", fb_rgb_to_pixel(LIM_DIM));

        int ml = my_strlen(m);
        if (ml > msg_max) ml = msg_max;
        char buf[96];
        for (int k = 0; k < ml; k++) buf[k] = m[k];
        buf[ml] = 0;
        fb_draw_text_t(x0 + 16 + 24, y, buf, fb_rgb_to_pixel(LIM_WHITE));

        int last = (i == total - 1);
        if (last) {
            fb_draw_text_t(badge_x, y, ">>", fb_rgb_to_pixel(LIM_ACCENT));
        } else if (ml >= 3 && m[0] == 'N' && m[1] == 'o' && m[2] == ' ') {
            fb_draw_text_t(badge_x, y, "[!!]", fb_rgb_to_pixel(LIM_WARN));
        } else {
            fb_draw_text_t(badge_x, y, "[OK]", fb_rgb_to_pixel(LIM_GREEN));
        }
    }
}

static void draw_progress(uint32_t* bb, int fw, int fh, int progress) {
    int total = 31;
    int p = progress; if (p < 0) p = 0; if (p > total) p = total;

    int bar_x = (int)(fw * 0.12);
    int bar_w = (int)(fw * 0.76);
    int bar_y = fh - 72;
    int bar_h = 8;

    blend_rect(bb, fw, fh, bar_x, bar_y, bar_w, bar_h, 0x223049, 220);
    int fill = bar_w * p / total;
    if (fill > 0) blend_rect(bb, fw, fh, bar_x, bar_y, fill, bar_h, LIM_ACCENT, 255);

    int v = p * 100 / total;
    char pct[16]; int n = 0;
    if (v == 0) pct[n++] = '0';
    else while (v > 0) { pct[n++] = '0' + (char)(v % 10); v /= 10; }
    for (int i = 0; i < n / 2; i++) { char t = pct[i]; pct[i] = pct[n - 1 - i]; pct[n - 1 - i] = t; }
    pct[n++] = '%'; pct[n] = 0;

    fb_draw_text_t(bar_x + bar_w + 16, bar_y - 6, pct, fb_rgb_to_pixel(LIM_DIM));
    fb_draw_text_t(bar_x, bar_y + 18, "Booting EigenOS...", fb_rgb_to_pixel(LIM_DIM));
}

void limine_boot_render(int progress) {
    int fw = (int)get_fb_width(), fh = (int)get_fb_height();
    if (fw <= 0 || fh <= 0) return;
    uint32_t* bb = fb_get_back_buffer();

    if (!g_bg_ready) build_background();
    if (g_bg_ready) {
        __builtin_memcpy(bb, g_bg, (size_t)fw * (size_t)fh * 4);
    } else {
        for (int i = 0; i < fw * fh; i++) bb[i] = fb_rgb_to_pixel(LIM_TINT);
    }

    /* small brand mark, top-left over the image */
    fb_draw_text_t(24, 18, "EIGENOS", fb_rgb_to_pixel(LIM_ACCENT2));

    draw_init_log(bb, fw, fh);
    draw_progress(bb, fw, fh, progress);

    fb_swap_buffers();
}
