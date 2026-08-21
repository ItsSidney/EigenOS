/***************************************************************/
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/***************************************************************/
/* Eigen OS - Image Viewer (Ring 3)
 * Supports BMP, PNG (libpng) and JPEG (libjpeg). On first launch it also
 * generates /demo.png and /demo.jpg from a synthetic gradient so the
 * codecs can be exercised without any external file. */
#include "userlib.h"
#include "userui.h"
#include "vector_icons.h"
#include "file_dialog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>

#include <png.h>
#include <jpeglib.h>

#define WIN_W   640
#define WIN_H   480
#define MAX_EVS 32

static int       win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t  cur_w = WIN_W, cur_h = WIN_H;
static char      file_path[256] = "";
static int       zoom_level = 2;   /* 0=25% 1=50% 2=100% 3=150% 4=200% */
static int       pan_x = 0, pan_y = 0;
static const char* zoom_labels[] = {"25%","50%","100%","150%","200%"};
static float     zoom_scale[] = {0.25f,0.5f,1.0f,1.5f,2.0f};
static uint8_t   file_buf[1048576];
static int       file_buf_len = 0;

/* decoded RGB bitmap (3 bytes/pixel), rendered with zoom/pan */
static uint8_t*  deco = NULL;
static int       deco_w = 0, deco_h = 0;
static int       have_deco = 0;

static ui_t      g_ui;

/* ───────────────────────── PNG / JPEG demo generators ───────────────────── */

static void png_write_cb(png_structp png, png_bytep d, png_size_t n) {
    typedef struct { uint8_t* data; size_t len, cap; } dynbuf;
    dynbuf* b = (dynbuf*)png_get_io_ptr(png);
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 65536;
        while (nc < b->len + n) nc *= 2;
        b->data = realloc(b->data, nc);
        b->cap = nc;
    }
    memcpy(b->data + b->len, d, n);
    b->len += n;
}
static void png_flush_cb(png_structp png) { (void)png; }

static int png_encode_mem(const uint8_t* rgb, int w, int h, uint8_t** out, unsigned long* outsize) {
    typedef struct { uint8_t* data; size_t len, cap; } dynbuf;
    dynbuf b = {0};
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return -1;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); return -1; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); free(b.data); return -1; }
    png_set_write_fn(png, &b, png_write_cb, png_flush_cb);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; y++) png_write_row(png, rgb + (size_t)y * w * 3);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    *out = b.data; *outsize = (unsigned long)b.len;
    return 0;
}

static int jpeg_encode_mem(const uint8_t* rgb, int w, int h, uint8_t** out, unsigned long* outsize) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    unsigned char* buf = NULL;
    unsigned long sz = 0;
    jpeg_mem_dest(&cinfo, &buf, &sz);
    cinfo.image_width = (JDIMENSION)w;
    cinfo.image_height = (JDIMENSION)h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    JSAMPROW row = (JSAMPROW)malloc((size_t)w * 3);
    for (int y = 0; y < h; y++) {
        memcpy(row, rgb + (size_t)y * w * 3, (size_t)w * 3);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    free(row);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    *out = buf; *outsize = sz;
    return 0;
}

static void make_demos(void) {
    int w = 128, h = 96;
    uint8_t* rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            rgb[i]     = (uint8_t)((x * 2) & 0xFF);
            rgb[i + 1] = (uint8_t)((y * 2) & 0xFF);
            rgb[i + 2] = (uint8_t)((x + y) & 0xFF);
        }
    uint8_t* png = NULL; unsigned long psz = 0;
    if (png_encode_mem(rgb, w, h, &png, &psz) == 0 && psz > 0)
        eigen_fs_write_file("/demo.png", png, (int)psz);
    free(png);
    uint8_t* jpg = NULL; unsigned long jsz = 0;
    if (jpeg_encode_mem(rgb, w, h, &jpg, &jsz) == 0 && jsz > 0)
        eigen_fs_write_file("/demo.jpg", jpg, (int)jsz);
    free(jpg);
    free(rgb);
}

/* ───────────────────────── decoders (in-memory) ─────────────────────────── */

typedef struct { const uint8_t* data; size_t size, pos; jmp_buf jb; } png_memsrc;
static void png_read_cb(png_structp png, png_bytep o, png_size_t n) {
    png_memsrc* m = (png_memsrc*)png_get_io_ptr(png);
    if (m->pos + n > m->size) longjmp(m->jb, 1);
    memcpy(o, m->data + m->pos, n);
    m->pos += n;
}

static int png_decode_mem(const uint8_t* data, int len, uint8_t** out, int* w, int* h) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return -1;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); return -1; }

    png_memsrc s; s.data = data; s.size = (size_t)len; s.pos = 0;
    if (setjmp(s.jb)) { png_destroy_read_struct(&png, &info, NULL); return -1; }
    png_set_read_fn(png, &s, (png_rw_ptr)png_read_cb);

    png_read_info(png, info);
    int iw = (int)png_get_image_width(png, info);
    int ih = (int)png_get_image_height(png, info);
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_strip_alpha(png);
    png_read_update_info(png, info);

    png_uint_32 rb = png_get_rowbytes(png, info);
    uint8_t* raw = malloc((size_t)ih * rb);
    png_bytep* rows = malloc((size_t)ih * sizeof(png_bytep));
    for (int y = 0; y < ih; y++) rows[y] = raw + (size_t)y * rb;
    png_read_image(png, rows);
    png_read_end(png, NULL);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);

    *out = raw; *w = iw; *h = ih;
    return 0;
}

static int jpeg_decode_mem(const uint8_t* data, int len, uint8_t** out, int* w, int* h) {
    struct jpeg_decompress_struct dinfo;
    struct jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, (unsigned char*)data, (unsigned long)len);
    jpeg_read_header(&dinfo, TRUE);
    dinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dinfo);
    int iw = (int)dinfo.output_width;
    int ih = (int)dinfo.output_height;
    uint8_t* raw = malloc((size_t)iw * ih * 3);
    JSAMPROW row = malloc((size_t)iw * 3);
    int y = 0;
    while (dinfo.output_scanline < (JDIMENSION)ih) {
        jpeg_read_scanlines(&dinfo, &row, 1);
        memcpy(raw + (size_t)y * iw * 3, row, (size_t)iw * 3);
        y++;
    }
    free(row);
    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    *out = raw; *w = iw; *h = ih;
    return 0;
}

static int bmp_decode_mem(const uint8_t* data, int len, uint8_t** out, int* w, int* h) {
    if (len < 54 || data[0] != 'B' || data[1] != 'M') return -1;
    uint32_t offset = *(const uint32_t*)(data + 10);
    int32_t iw = *(const int32_t*)(data + 18);
    int32_t ih = *(const int32_t*)(data + 22);
    uint16_t bpp = *(const uint16_t*)(data + 28);
    if ((bpp != 24 && bpp != 32) || iw <= 0 || ih <= 0) return -1;
    int stride = (bpp == 24) ? ((iw * 3 + 3) & ~3) : (iw * 4);
    int bppx = bpp / 8;
    uint8_t* raw = malloc((size_t)iw * ih * 3);
    for (int y = 0; y < ih; y++) {
        const uint8_t* src = data + offset + (size_t)(ih - 1 - y) * stride;
        uint8_t* dst = raw + (size_t)y * iw * 3;
        for (int x = 0; x < iw; x++) {
            const uint8_t* p = src + (size_t)x * bppx;
            dst[x*3]   = p[2];
            dst[x*3+1] = p[1];
            dst[x*3+2] = p[0];
        }
    }
    *out = raw; *w = iw; *h = ih;
    return 0;
}

static int decode_loaded(void) {
    if (file_buf_len < 8) return -1;
    if (deco) { free(deco); deco = NULL; }
    have_deco = 0;
    int r = -1;
    if (file_buf[0] == 0x89 && file_buf[1] == 'P')
        r = png_decode_mem(file_buf, file_buf_len, &deco, &deco_w, &deco_h);
    else if (file_buf[0] == 0xFF && file_buf[1] == 0xD8)
        r = jpeg_decode_mem(file_buf, file_buf_len, &deco, &deco_w, &deco_h);
    else
        r = bmp_decode_mem(file_buf, file_buf_len, &deco, &deco_w, &deco_h);
    if (r == 0 && deco) { have_deco = 1; return 0; }
    return -1;
}

static void load_image(const char* path) {
    if (!path || !path[0]) return;
    file_buf_len = eigen_fs_read_file(path, file_buf, sizeof(file_buf));
    decode_loaded();
}

static void open_dialog(void) {
    char chosen[256] = "";
    if (eigen_dialog_open(chosen, sizeof(chosen), ".png") && chosen[0]) {
        strncpy(file_path, chosen, 255);
        file_path[255] = 0;
        load_image(file_path);
        pan_x = 0; pan_y = 0; zoom_level = 2;
    }
}

/* ───────────────────────── rendering ────────────────────────────────────── */

static void blit_rgb(const uint8_t* rgb, int iw, int ih,
                     int cx, int cy, int cw, int ch) {
    float scale = zoom_scale[zoom_level];
    int dw = (int)(iw * scale);
    int dh = (int)(ih * scale);
    int base_x = cx + pan_x + (cw - dw) / 2;
    int base_y = cy + pan_y + (ch - dh) / 2;
    for (int dy = cy; dy < cy + ch; dy++) {
        int sy = (int)((dy - base_y) / scale + 0.5f);
        if (sy < 0 || sy >= ih) continue;
        for (int dx = cx; dx < cx + cw; dx++) {
            int sx = (int)((dx - base_x) / scale + 0.5f);
            if (sx < 0 || sx >= iw) continue;
            const uint8_t* p = rgb + (size_t)(sy * iw + sx) * 3;
            uint32_t c = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            win_fb[dy * cur_w + dx] = c;
        }
    }
}

static void render_all(void) {
    if (!win_fb) return;
    uint32_t bg=0x0D1117, bar=0x161B22, border=0x30363D, text=0xE6EDF3, dim=0x8B949E;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, 38, bar);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 37, cur_w, 1, border);
    eigen_draw_text(win_fb, cur_w, cur_h, 12, 11, "IMAGE VIEWER", dim);
    if (file_path[0]) {
        char lbl[320];
        snprintf(lbl, sizeof(lbl), "  |  %s  [%s]", file_path, zoom_labels[zoom_level]);
        eigen_draw_text(win_fb, cur_w, cur_h, 114, 11, lbl, text);
    } else {
        eigen_draw_text(win_fb, cur_w, cur_h, 120, 11, "No file  |  [O] Open  |  +/- zoom  |  WASD pan  |  P/J demos", dim);
    }
    if (ui_button(&g_ui, (int)cur_w - 100, 6, 88, 26, "Open File"))
        open_dialog();
    int cx = 12, cy = 50, cw = cur_w - 24, ch = cur_h - 62;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, cx, cy, cw, ch, 0x111418);
    eigen_draw_rect(win_fb, cur_w, cur_h, cx, cy, cw, ch, border);
    if (have_deco) {
        blit_rgb(deco, deco_w, deco_h, cx, cy, cw, ch);
    } else if (file_path[0]) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Cannot render: %s", file_path);
        eigen_draw_text(win_fb, cur_w, cur_h, cx+20, cy+ch/2-8, msg, 0xF85149);
        eigen_draw_text(win_fb, cur_w, cur_h, cx+20, cy+ch/2+12, "Unsupported format or file not found", dim);
    } else {
        eigen_draw_text(win_fb, cur_w, cur_h, cx+cw/2-110, cy+ch/2-16, "Click Open File or press O to browse", dim);
        eigen_draw_text(win_fb, cur_w, cur_h, cx+cw/2-110, cy+ch/2+8,  "Supports PNG / JPEG / BMP", dim);
    }
    int sy = cur_h - 22;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, sy, cur_w, 22, bar);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, sy, cur_w, 1, border);
    char stat[80];
    snprintf(stat, sizeof(stat), "Zoom: %s  |  Pan: %d,%d  |  %dx%d",
             zoom_labels[zoom_level], pan_x, pan_y, deco_w, deco_h);
    eigen_draw_text(win_fb, cur_w, cur_h, 12, sy+4, stat, dim);
    eigen_win_flush(win_id);
}

int main(int argc, char* argv[]) {
    make_demos();
    if (argc > 1 && argv[1]) strncpy(file_path, argv[1], 255);
    else                     strncpy(file_path, "/demo.png", 255);
    file_path[255] = 0;

    win_id = eigen_win_create(100, 70, WIN_W, WIN_H, "Image Viewer");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);
    load_image(file_path);

    eigen_ev_t evs[MAX_EVS];
    for (;;) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);
        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);
        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) goto done;
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100) continue;
                char k = (char)ev->a;
                if      (k == '+' || k == '=') { if (zoom_level < 4) zoom_level++; }
                else if (k == '-')             { if (zoom_level > 0) zoom_level--; }
                else if (k == 'w' || k == 'W') pan_y += 16;
                else if (k == 's' || k == 'S') pan_y -= 16;
                else if (k == 'a' || k == 'A') pan_x += 16;
                else if (k == 'd' || k == 'D') pan_x -= 16;
                else if (k == 'r' || k == 'R') { pan_x=0; pan_y=0; zoom_level=2; }
                else if (k == 'o' || k == 'O') open_dialog();
                else if (k == 'p' || k == 'P') { strncpy(file_path,"/demo.png",255); file_path[255]=0; load_image(file_path); }
                else if (k == 'j' || k == 'J') { strncpy(file_path,"/demo.jpg",255); file_path[255]=0; load_image(file_path); }
            }
        }
        render_all();
        ui_end(&g_ui);
        eigen_sleep_ms(30);
    }
done:
    if (deco) free(deco);
    eigen_win_close(win_id);
    return 0;
}
