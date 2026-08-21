/* jpegtest.c — verify the EigenOS libjpeg port (encode -> in-memory -> decode). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <jpeglib.h>

#define W 16
#define H 16
#define TOL 48   /* JPEG is lossy; allow small per-channel deviation */

static int fails = 0;
#define CHECK(c, msg) do { \
    if (!(c)) { printf("[JPEGTEST] FAIL: %s\n", msg); fails++; } \
    else      { printf("[JPEGTEST] ok:   %s\n", msg); } \
} while (0)

/* libjpeg longjmp-based error handling */
struct my_errmgr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};
static void my_error_exit(j_common_ptr cinfo) {
    struct my_errmgr *m = (struct my_errmgr *)cinfo->err;
    longjmp(m->jb, 1);
}

int main(void) {
    const int w = W, h = H;

    /* original RGB image: a smooth gradient so JPEG stays close to exact */
    unsigned char *orig = malloc((size_t)w * h * 3);
    JSAMPLE *rows[H];
    for (int y = 0; y < h; y++) {
        rows[y] = orig + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            orig[i]     = (unsigned char)(x * 16);
            orig[i + 1] = (unsigned char)(y * 16);
            orig[i + 2] = (unsigned char)((x + y) * 8);
        }
    }

    /* ---- encode to memory ---- */
    struct jpeg_compress_struct cinfo;
    struct my_errmgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.jb)) { printf("[JPEGTEST] FAIL: encode error\n"); return 1; }

    jpeg_create_compress(&cinfo);
    unsigned char *outbuf = NULL;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuf, &outsize);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 92, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    jpeg_write_scanlines(&cinfo, rows, h);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    CHECK(outsize > 0, "jpeg produced bytes");
    printf("[JPEGTEST] encoded %lu bytes\n", outsize);

    /* ---- decode from memory ---- */
    struct jpeg_decompress_struct dinfo;
    struct my_errmgr jerr2;
    dinfo.err = jpeg_std_error(&jerr2.pub);
    jerr2.pub.error_exit = my_error_exit;
    if (setjmp(jerr2.jb)) { printf("[JPEGTEST] FAIL: decode error\n"); return 1; }

    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, outbuf, outsize);
    jpeg_read_header(&dinfo, TRUE);
    jpeg_start_decompress(&dinfo);
    CHECK(dinfo.output_width == (JDIMENSION)w, "decoded width matches");
    CHECK(dinfo.output_height == (JDIMENSION)h, "decoded height matches");
    CHECK(dinfo.output_components == 3, "decoded is 3-component (RGB)");

    unsigned char *dec = malloc((size_t)dinfo.output_width * dinfo.output_height * 3);
    JSAMPLE *drows[dinfo.output_height];
    for (JDIMENSION y = 0; y < dinfo.output_height; y++)
        drows[y] = dec + (size_t)y * dinfo.output_width * 3;
    while (dinfo.output_scanline < dinfo.output_height)
        jpeg_read_scanlines(&dinfo, &drows[dinfo.output_scanline],
                            dinfo.output_height - dinfo.output_scanline);
    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);

    /* ---- compare with tolerance ---- */
    int maxdiff = 0, mismatches = 0;
    int total = w * h * 3;
    for (int i = 0; i < total; i++) {
        int d = orig[i] - dec[i];
        if (d < 0) d = -d;
        if (d > maxdiff) maxdiff = d;
        if (d > TOL) mismatches++;
    }
    printf("[JPEGTEST] max pixel diff=%d, mismatches(>%d)=%d\n", maxdiff, TOL, mismatches);
    CHECK(maxdiff <= TOL, "decoded pixels within tolerance");

    free(orig); free(dec); free(outbuf);
    if (fails == 0) printf("[JPEGTEST] ALL PASS\n");
    else            printf("[JPEGTEST] %d FAILURES\n", fails);
    return fails ? 1 : 0;
}
