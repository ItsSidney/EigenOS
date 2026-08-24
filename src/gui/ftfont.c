/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/
/*
 * Kernel-side FreeType text engine for the EigenOS shell.
 *
 * - Face: DejaVuSans shipped as a Limine boot module ("DejaVuSans"),
 *   loaded in-place via user_module_find() — zero copy.
 * - Rasterization: FreeType grayscale (8bpp), cached.
 * - Cache: open-addressed table keyed by (glyph_index, px size).
 *   Each entry holds the alpha bitmap + metrics. Entries are never
 *   freed (the working set for shell UI is a few hundred glyphs).
 * - Blitting goes through gfx_blend_pixel honoring the gfx clip rect,
 *   so text clips correctly inside scrolled panels.
 */

#include "gui/ftfont.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "kernel/mem/kheap.h"
#include "kernel/log.h"
#include "drivers/video/gfx.h"
#include <string.h>

extern int user_module_find(const char* name, const void** data, uint64_t* size);

/* ── state ─────────────────────────────────────────────────── */
static FT_Library g_lib = 0;
static FT_Face    g_face = 0;
static int        g_ready = 0;
static int        g_cur_px = -1;      /* pixel size currently set on face */

#define FTCACHE_SIZE 1024
typedef struct {
    uint32_t key;        /* hash key: glyph id + px — 0 = empty */
    uint16_t px;
    uint16_t w, h;
    int16_t  bx, by;     /* left bearing / bitmap top    */
    int16_t  adv;        /* advance in pixels            */
    int16_t  lru;        /* stamp for eviction           */
    uint8_t* bits;       /* w*h alpha bytes (NULL = space) */
} ftcache_ent_t;

static ftcache_ent_t g_cache[FTCACHE_SIZE];
static uint16_t g_lru_clock = 1;

/* per-size vertical metrics cache */
#define FTSIZES_MAX 12
typedef struct { int px, h, asc, used; } ftsize_ent_t;
static ftsize_ent_t g_sizes[FTSIZES_MAX];

/* ── helpers ───────────────────────────────────────────────── */

int ftfont_ready(void) { return g_ready; }

int ftfont_init(void) {
    if (g_ready) return 1;

    const void* data = 0;
    uint64_t size = 0;
    if (user_module_find("DejaVuSans", &data, &size) != 0 || !data || !size) {
        klog("[FTFONT] DejaVuSans boot module not found\n");
        return 0;
    }

    if (!g_lib) {
        if (FT_Init_FreeType(&g_lib) != 0) {
            klog("[FTFONT] FT_Init_FreeType failed\n");
            g_lib = 0;
            return 0;
        }
    }
    if (!g_face) {
        if (FT_New_Memory_Face(g_lib, (const FT_Byte*)data, (FT_Long)size, 0, &g_face) != 0) {
            klog("[FTFONT] FT_New_Memory_Face failed\n");
            g_face = 0;
            return 0;
        }
    }
    g_cur_px = -1;
    memset(g_sizes, 0, sizeof(g_sizes));
    g_ready = 1;
    klog("[FTFONT] DejaVuSans ready\n");
    return 1;
}

static int ft_set_px(int px) {
    if (!g_ready || px == g_cur_px) return g_ready ? 0 : -1;
    if (px < 6) px = 6;
    if (px > 72) px = 72;
    if (FT_Set_Pixel_Sizes(g_face, 0, (FT_UInt)px) != 0) return -1;
    g_cur_px = px;
    return 0;
}

static ftsize_ent_t* ft_size_metrics(int px) {
    for (int i = 0; i < FTSIZES_MAX; i++)
        if (g_sizes[i].used && g_sizes[i].px == px) return &g_sizes[i];
    if (ft_set_px(px) != 0) return 0;
    for (int i = 0; i < FTSIZES_MAX; i++) {
        if (!g_sizes[i].used) {
            g_sizes[i].used = 1;
            g_sizes[i].px = px;
            g_sizes[i].asc = (g_face->size->metrics.ascender + 32) >> 6;
            g_sizes[i].h = ((g_face->size->metrics.ascender -
                             g_face->size->metrics.descender) + 32) >> 6;
            if (g_sizes[i].h <= 0) g_sizes[i].h = px + 2;
            return &g_sizes[i];
        }
    }
    /* table full: reuse slot 0 */
    g_sizes[0].px = px;
    g_sizes[0].asc = (g_face->size->metrics.ascender + 32) >> 6;
    g_sizes[0].h = ((g_face->size->metrics.ascender -
                     g_face->size->metrics.descender) + 32) >> 6;
    return &g_sizes[0];
}

int ftfont_height(int px) {
    if (!ftfont_init()) return px + 2;
    ftsize_ent_t* m = ft_size_metrics(px);
    return m ? m->h : px + 2;
}

int ftfont_ascent(int px) {
    if (!ftfont_init()) return px;
    ftsize_ent_t* m = ft_size_metrics(px);
    return m ? m->asc : px;
}

/* decode one codepoint from UTF-8; byte length returned via *len_out */
static unsigned ft_cp_to_utf8(const char* s, unsigned* len_out) {
    unsigned char c0 = (unsigned char)s[0];
    unsigned cp, len;
    if (c0 < 0x80) { cp = c0; len = 1; }
    else if ((c0 & 0xE0) == 0xC0 && (unsigned char)s[1]) {
        cp = ((c0 & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu); len = 2;
    } else if ((c0 & 0xF0) == 0xE0 && (unsigned char)s[1] && (unsigned char)s[2]) {
        cp = ((c0 & 0x0Fu) << 12) | (((unsigned char)s[1] & 0x3Fu) << 6)
           | ((unsigned char)s[2] & 0x3Fu);
        len = 3;
    } else { cp = c0; len = 1; } /* invalid byte: pass through */
    *len_out = len;
    return cp;
}

static ftcache_ent_t* ft_glyph(unsigned cp, int px) {
    unsigned gi = FT_Get_Char_Index(g_face, cp);
    /* key distinguishes (glyph, size); unmapped chars get a cp-based key */
    uint32_t key = ((uint32_t)(gi & 0x3FFFFu) << 15)
                 | (uint32_t)((gi ? cp : 0x1FFFFu) & 0x7FFFu);

    /* find or claim a slot */
    unsigned h = (unsigned)(key * 2654435761u + (unsigned)px * 97u) % FTCACHE_SIZE;
    ftcache_ent_t* free_slot = 0;
    for (unsigned probe = 0; probe < 64; probe++) {
        ftcache_ent_t* e = &g_cache[(h + probe) % FTCACHE_SIZE];
        if (e->bits && e->key == key && e->px == px) {
            e->lru = ++g_lru_clock;
            return e;
        }
        if (!e->bits && !free_slot) free_slot = e;
    }
    if (!free_slot) {
        /* evict LRU along the probe path */
        ftcache_ent_t* lru = &g_cache[h % FTCACHE_SIZE];
        for (unsigned probe = 0; probe < 128; probe++) {
            ftcache_ent_t* e = &g_cache[(h + probe) % FTCACHE_SIZE];
            if (e->lru < lru->lru) lru = e;
        }
        if (lru->bits) { kfree(lru->bits); lru->bits = 0; }
        free_slot = lru;
    }

    if (ft_set_px(px) != 0) return 0;
    if (FT_Load_Glyph(g_face, gi, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
        return 0;

    FT_GlyphSlot gs = g_face->glyph;
    FT_Bitmap* bm = &gs->bitmap;
    int w = (int)bm->width, hh = (int)bm->rows;
    uint8_t* bits = 0;
    if (w > 0 && hh > 0) {
        bits = kmalloc((size_t)(w * hh));
        if (!bits) return 0;
        for (int y = 0; y < hh; y++) {
            const unsigned char* src = bm->buffer + (size_t)y * bm->pitch;
            for (int x = 0; x < w; x++) bits[y * w + x] = src[x];
        }
    }

    free_slot->key = key;
    free_slot->px = (uint16_t)px;
    free_slot->w = (uint16_t)w;
    free_slot->h = (uint16_t)hh;
    free_slot->bx = (int16_t)(gs->bitmap_left);
    free_slot->by = (int16_t)(gs->bitmap_top);
    free_slot->adv = (int16_t)((gs->advance.x + 32) >> 6);
    free_slot->lru = ++g_lru_clock;
    free_slot->bits = bits; /* may be NULL for whitespace glyphs */
    return free_slot;
}

int ftfont_width(const char* s, int px) {
    if (!s) return 0;
    if (!ftfont_init()) return 0;
    int adv_total = 0;
    while (*s) {
        unsigned len = 0;
        unsigned cp = ft_cp_to_utf8(s, &len);
        s += len;
        if (!cp) break;
        ftcache_ent_t* e = ft_glyph(cp, px);
        adv_total += e ? e->adv : (px / 2);
    }
    return adv_total;
}

void ftfont_draw(int x, int y, const char* s, uint32_t rgb, int px) {
    if (!s || !s[0]) return;
    if (!ftfont_init()) return;
    ftsize_ent_t* m = ft_size_metrics(px);
    if (!m) return;
    int baseline = y + m->asc;
    int pen = x;
    int cx0, cy0, cx1, cy1;
    gfx_get_clip(&cx0, &cy0, &cx1, &cy1);

    while (*s) {
        unsigned len = 0;
        unsigned cp = ft_cp_to_utf8(s, &len);
        s += len;
        if (!cp) break;
        ftcache_ent_t* e = ft_glyph(cp, px);
        if (!e) { pen += px / 3; continue; }
        if (e->bits) {
            int gx = pen + e->bx;
            int gy = baseline - e->by;
            if (gx + e->w >= cx0 && gx <= cx1 && gy + e->h >= cy0 && gy <= cy1) {
                for (int r = 0; r < e->h; r++) {
                    const uint8_t* row = e->bits + (size_t)r * e->w;
                    int py = gy + r;
                    if (py < cy0 || py >= cy1) continue;
                    for (int c = 0; c < e->w; c++) {
                        uint8_t a = row[c];
                        if (!a) continue;
                        int pxx = gx + c;
                        if (pxx < cx0 || pxx >= cx1) continue;
                        gfx_blend_pixel(pxx, py, rgb, a);
                    }
                }
            }
        }
        pen += e->adv;
    }
}

int ftfont_draw_trunc(int x, int y, int max_w, const char* s,
                      uint32_t rgb, int px) {
    if (!s || !s[0]) return x;
    int full = ftfont_width(s, px);
    if (full <= max_w) {
        ftfont_draw(x, y, s, rgb, px);
        return x + full;
    }
    /* binary-search longest prefix that fits with ".." appended */
    int lo = 0, hi = 0;
    while (s[hi]) hi++;
    int best = 0;
    static char buf[160];
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (mid > 150) mid = 150;
        int j = 0;
        for (; j < mid; j++) buf[j] = s[j];
        buf[j] = 0;
        int w = ftfont_width(buf, px) + ftfont_width("..", px);
        if (w <= max_w) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    int j = 0;
    for (; j < best && j < 150; j++) buf[j] = s[j];
    if (j > 148) j = 148;
    buf[j++] = '.'; buf[j++] = '.'; buf[j] = 0;
    ftfont_draw(x, y, buf, rgb, px);
    return x + ftfont_width(buf, px);
}
