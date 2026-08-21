/* pngtest.c — verify the EigenOS libpng port (encode -> in-memory -> decode). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <png.h>

/* In-memory I/O so the test needs no filesystem. */
static unsigned char png_buf[1 << 20];
static png_size_t     png_len;
static png_size_t     png_pos;

static void
write_cb(png_structp png, png_bytep data, png_size_t len) {
    (void)png;
    if (png_len + len <= sizeof(png_buf)) {
        memcpy(png_buf + png_len, data, len);
        png_len += len;
    }
}
static void flush_cb(png_structp png) { (void)png; }
static void
read_cb(png_structp png, png_bytep data, png_size_t len) {
    (void)png;
    if (png_pos + len <= png_len) {
        memcpy(data, png_buf + png_pos, len);
        png_pos += len;
    }
}

#define W 8
#define H 8

static int fails = 0;
#define CHECK(c, msg) do { \
    if (!(c)) { printf("[PNGTEST] FAIL: %s\n", msg); fails++; } \
    else      { printf("[PNGTEST] ok:   %s\n", msg); } \
} while (0)

int main(void) {
    const int w = W, h = H;

    /* original RGB image with a known, varied pattern */
    unsigned char *orig = malloc((size_t)w * h * 3);
    unsigned char **rows = malloc(sizeof(unsigned char *) * h);
    for (int y = 0; y < h; y++) {
        rows[y] = orig + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            orig[i]     = (unsigned char)(x * 32);
            orig[i + 1] = (unsigned char)(y * 32);
            orig[i + 2] = (unsigned char)((x + y) * 16);
        }
    }

    /* ---- encode ---- */
    png_len = 0; png_pos = 0;
    png_structp wp = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    CHECK(wp != NULL, "png_create_write_struct");
    png_infop wi = png_create_info_struct(wp);
    CHECK(wi != NULL, "png_create_info_struct (write)");
    if (!wp || !wi) { return 1; }
    if (setjmp(png_jmpbuf(wp))) { printf("[PNGTEST] FAIL: encode error\n"); return 1; }
    png_set_write_fn(wp, NULL, write_cb, flush_cb);
    png_set_IHDR(wp, wi, w, h, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(wp, wi);
    png_write_image(wp, rows);
    png_write_end(wp, wi);
    png_destroy_write_struct(&wp, &wi);
    CHECK(png_len > 0, "png produced bytes");

    /* ---- decode ---- */
    png_pos = 0;
    png_structp rp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    CHECK(rp != NULL, "png_create_read_struct");
    png_infop ri = png_create_info_struct(rp);
    CHECK(ri != NULL, "png_create_info_struct (read)");
    if (!rp || !ri) { return 1; }
    if (setjmp(png_jmpbuf(rp))) { printf("[PNGTEST] FAIL: decode error\n"); return 1; }
    png_set_read_fn(rp, NULL, read_cb);
    png_read_info(rp, ri);

    png_uint_32 rw = 0, rh = 0;
    int bit_depth = 0, color_type = 0;
    png_get_IHDR(rp, ri, &rw, &rh, &bit_depth, &color_type, NULL, NULL, NULL);
    CHECK(rw == (png_uint_32)w && rh == (png_uint_32)h, "decoded dimensions match");
    CHECK(bit_depth == 8, "decoded bit depth == 8");
    CHECK(color_type == PNG_COLOR_TYPE_RGB, "decoded color type == RGB");

    png_read_update_info(rp, ri);
    unsigned char *dec = malloc((size_t)rw * rh * 3);
    unsigned char **drows = malloc(sizeof(unsigned char *) * rh);
    for (png_uint_32 y = 0; y < rh; y++) drows[y] = dec + (size_t)y * rw * 3;
    png_read_image(rp, drows);
    png_read_end(rp, ri);
    png_destroy_read_struct(&rp, &ri, NULL);

    int match = 0;
    for (int i = 0; i < w * h * 3; i++) if (dec[i] == orig[i]) match++;
    CHECK(match == w * h * 3, "all pixel bytes round-trip");

    free(orig); free(rows); free(dec); free(drows);
    if (fails == 0) printf("[PNGTEST] ALL PASS\n");
    else            printf("[PNGTEST] %d FAILURES\n", fails);
    return fails ? 1 : 0;
}
