/***************************************************************/
/*  EigenUI Font — implementation                                */
/***************************************************************/
#include "eui_font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "userlib.h"
#include <stdlib.h>
#include <string.h>

static FT_Library g_lib = 0;
static FT_Face    g_face = 0;
static unsigned char* g_font_data = 0;

int eui_font_init(void) {
    if (g_face) return 0;
    if (FT_Init_FreeType(&g_lib)) return -1;

    long cap = 4 * 1024 * 1024;
    g_font_data = (unsigned char*)eigen_malloc((size_t)cap);
    if (!g_font_data) { FT_Done_FreeType(g_lib); g_lib = 0; return -1; }
    long size = eigen_load_module("DejaVuSans", g_font_data, (uint64_t)cap);
    if (size <= 0) { eigen_free(g_font_data); g_font_data = 0; FT_Done_FreeType(g_lib); g_lib = 0; return -1; }
    if (FT_New_Memory_Face(g_lib, g_font_data, (FT_Long)size, 0, &g_face)) {
        eigen_free(g_font_data); g_font_data = 0; FT_Done_FreeType(g_lib); g_lib = 0; return -1;
    }
    return 0;
}

static void blend_dev(eui_canvas* c, int x, int y, eui_color col, uint8_t a) {
    if (x < c->clip.x || y < c->clip.y || x >= c->clip.x + c->clip.w || y >= c->clip.y + c->clip.h)
        return;
    c->buf[y * c->w + x] = eui_blend(c->buf[y * c->w + x], col, a);
}

int eui_font_draw(eui_canvas* c, int x, int y, int size, eui_color col, const char* s) {
    if (!g_face || !s) return 0;
    float sc = c->scale;
    int dev_size = (int)((float)size * sc + 0.5f);
    if (dev_size < 1) dev_size = 1;
    if (FT_Set_Pixel_Sizes(g_face, 0, (FT_UInt)dev_size)) return 0;

    int pen_x = (int)((float)x * sc + 0.5f);
    int baseline = (int)((float)(y + size) * sc + 0.5f);  /* approximate cap top */
    int adv_log = 0;

    for (const char* p = s; *p; p++) {
        unsigned int ch = (unsigned char)*p;
        if (ch == ' ') { pen_x += dev_size * 3 / 7; adv_log += size * 3 / 7; continue; }
        if (FT_Load_Char(g_face, (FT_ULong)ch, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = g_face->glyph;
        FT_Bitmap* bmp = &g->bitmap;
        int bl = g->bitmap_left, bt = g->bitmap_top;
        int adv = (int)(g->advance.x >> 6);
        if (bmp->buffer && bmp->width && bmp->rows) {
            for (unsigned int yy = 0; yy < bmp->rows; yy++)
                for (unsigned int xx = 0; xx < bmp->width; xx++) {
                    uint8_t v = bmp->buffer[yy * (unsigned int)bmp->pitch + xx];
                    if (!v) continue;
                    blend_dev(c, pen_x + bl + (int)xx, baseline - bt + (int)yy, col, v);
                }
        }
        pen_x += adv;
        adv_log += (int)((float)adv / sc + 0.5f);
    }
    return adv_log;
}

void eui_font_measure(eui_canvas* c, const char* s, int size, int* w, int* h) {
    float sc = c ? c->scale : 1.0f;
    int dev_size = (int)((float)size * sc + 0.5f);
    if (dev_size < 1) dev_size = 1;
    int width = 0;
    int height = dev_size;
    if (g_face) {
        if (!FT_Set_Pixel_Sizes(g_face, 0, (FT_UInt)dev_size)) {
            for (const char* p = s; *p; p++) {
                unsigned int ch = (unsigned char)*p;
                if (ch == ' ') { width += dev_size * 3 / 7; continue; }
                if (FT_Load_Char(g_face, (FT_ULong)ch, FT_LOAD_RENDER)) continue;
                width += (int)(g_face->glyph->advance.x >> 6);
            }
        }
    } else {
        /* fallback: 7px per char */
        width = (int)(eigen_strlen(s) * 7 * sc);
    }
    if (w) *w = (int)((float)width / sc + 0.5f);
    if (h) *h = size;
}
