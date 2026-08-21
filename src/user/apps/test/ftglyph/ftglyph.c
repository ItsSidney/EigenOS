/*
 * ftglyph — minimal FreeType glyph-render proof for EigenOS (M2 of the EFL port).
 *
 * Loads a TrueType font shipped as a Limine boot module ("DejaVuSans", from
 * /fonts/DejaVuSans.ttf) via eigen_load_module(), rasterizes a short string
 * with the bundled FreeType library, and BLITS the glyph bitmaps straight into
 * the app's own window buffer (visible in the GUI). Metrics also go to stdout
 * (the kernel serial log) for headless verification.
 */
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <userlib.h>

/* Blend an 8-bit glyph coverage value (a) between bg and fg. */
static uint32_t blend(uint32_t bg, uint32_t fg, uint8_t a) {
    int br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    int fr = (fg >> 16) & 0xFF, fgc = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    int r = (fr * a + br * (255 - a)) / 255;
    int g = (fgc * a + bgc * (255 - a)) / 255;
    int b = (fb * a + bb * (255 - a)) / 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

int main(int argc, char** argv) {
    printf("[ftglyph] FreeType glyph render test (window)\n");

    FT_Library lib;
    FT_Error err = FT_Init_FreeType(&lib);
    if (err) { printf("[ftglyph] FT_Init_FreeType failed: 0x%x\n", err); return 1; }
    printf("[ftglyph] FT_Init_FreeType OK (FreeType %d.%d.%d)\n",
           FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH);

    /* The font ships as a Limine boot module named "DejaVuSans" (basename of
       /fonts/DejaVuSans.ttf, extension stripped by the kernel). Load its bytes
       into a user buffer — do NOT use fopen (the in-OS FS doesn't expose it). */
    long cap = 2 * 1024 * 1024;
    unsigned char* data = malloc((size_t)cap);
    if (!data) { printf("[ftglyph] OOM allocating font buffer\n"); FT_Done_FreeType(lib); return 2; }
    long size = eigen_load_module("DejaVuSans", data, (uint64_t)cap);
    if (size <= 0) {
        printf("[ftglyph] FONT MODULE 'DejaVuSans' NOT FOUND (build missing font asset)\n");
        free(data); FT_Done_FreeType(lib); return 2;
    }
    printf("[ftglyph] loaded font %ld bytes from boot module 'DejaVuSans'\n", size);

    FT_Face face;
    err = FT_New_Memory_Face(lib, data, (FT_Long)size, 0, &face);
    if (err) {
        printf("[ftglyph] FT_New_Memory_Face failed: 0x%x\n", err);
        free(data); FT_Done_FreeType(lib); return 1;
    }
    printf("[ftglyph] face '%s' glyphs=%ld\n",
           face->family_name ? face->family_name : "?", (long)face->num_glyphs);

    err = FT_Set_Pixel_Sizes(face, 0, 64);
    if (err) {
        printf("[ftglyph] FT_Set_Pixel_Sizes failed: 0x%x\n", err);
        FT_Done_Face(face); free(data); FT_Done_FreeType(lib); return 1;
    }

    const char* text = (argc > 1) ? argv[1] : "Ei! 42";

    /* ---- window ---- */
    int win = eigen_win_create(120, 120, 560, 320, "ftglyph — FreeType");
    if (win < 0) {
        printf("[ftglyph] eigen_win_create failed (%d)\n", win);
        FT_Done_Face(face); free(data); FT_Done_FreeType(lib); return 1;
    }
    uint32_t* buf = (uint32_t*)eigen_win_map(win);
    uint32_t w = 0, h = 0;
    eigen_win_getsize(win, &w, &h);

    uint32_t bg = 0x101418;
    uint32_t fg = 0x4FD1C5;
    eigen_draw_fillrect(buf, (int)w, (int)h, 0, 0, (int)w, (int)h, bg);
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "FreeType %d.%d.%d  |  %s  (%ld glyphs)",
             FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH,
             face->family_name ? face->family_name : "?", (long)face->num_glyphs);
    eigen_draw_text(buf, (int)w, (int)h, 16, 14, hdr, 0xC8D3E0);

    int pen_x = 24;
    int baseline = 210;
    int lit_total = 0;
    for (const char* p = text; *p; p++) {
        if (*p == ' ') { pen_x += 32; continue; }
        err = FT_Load_Char(face, (FT_ULong)(unsigned char)*p, FT_LOAD_RENDER);
        if (err) { printf("[ftglyph] FT_Load_Char '%c' failed: 0x%x\n", *p, err); continue; }
        FT_GlyphSlot g = face->glyph;
        FT_Bitmap* bmp = &g->bitmap;
        printf("[ftglyph] '%c' -> %ux%u @(%ld,%ld) adv=%ld\n", *p,
               bmp->width, bmp->rows, (long)g->bitmap_left, (long)g->bitmap_top,
               (long)g->advance.x >> 6);
        for (unsigned int y = 0; y < bmp->rows; y++) {
            for (unsigned int x = 0; x < bmp->width; x++) {
                uint8_t v = bmp->buffer[y * (unsigned int)bmp->pitch + x];
                if (!v) continue;
                int px = pen_x + (int)g->bitmap_left + (int)x;
                int py = baseline - (int)g->bitmap_top + (int)y;
                eigen_draw_pixel(buf, (int)w, (int)h, px, py, blend(bg, fg, v));
                lit_total++;
            }
        }
        pen_x += (int)(g->advance.x >> 6);
    }
    printf("[ftglyph] rendered %d lit pixels total\n", lit_total);
    eigen_win_flush(win);
    printf("[ftglyph] FREETYPE GLYPH RENDER OK (window visible)\n");

    /* Keep the window open until closed or a key is pressed. */
    eigen_ev_t evs[8];
    for (;;) {
        int n = eigen_win_poll(win, evs, 8);
        int quit = 0;
        for (int i = 0; i < n; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE || evs[i].type == EIGEN_EV_KEY) quit = 1;
        }
        if (quit) break;
        eigen_sleep_ms(30);
    }
    eigen_win_close(win);

    FT_Done_Face(face);
    free(data);
    FT_Done_FreeType(lib);
    return 0;
}
