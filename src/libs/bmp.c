/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "libs/bmp.h"
#include "kernel/mem/kheap.h"
#include <stdint.h>
#include <string.h>

static uint32_t r32(const uint8_t* d, int o) {
    return (uint32_t)d[o] | ((uint32_t)d[o+1] << 8) | ((uint32_t)d[o+2] << 16) | ((uint32_t)d[o+3] << 24);
}
static uint16_t r16(const uint8_t* d, int o) {
    return (uint16_t)d[o] | ((uint16_t)d[o+1] << 8);
}
static int32_t r32s(const uint8_t* d, int o) {
    return (int32_t)r32(d, o);
}

// ── Bit replication: 5→8, 6→8, etc ──────────────────────────
static inline int rep_bits(int v, int bits) {
    if (bits >= 8) return v;
    int r = v;
    for (int s = bits; s < 8; s += bits)
        r |= (v << s);
    return r & 0xFF;
}

int bmp_decode(const uint8_t* data, int len, bmp_image_t* img) {
    if (!data || !img || len < 26) return 0;
    if (data[0] != 'B' || data[1] != 'M') return 0;

    uint32_t pix_off = r32(data, 10);
    uint32_t hdr_sz  = r32(data, 14);
    int w = 0, h = 0;
    uint16_t bpp = 0;
    uint32_t compression = 0;
    int palette_colors = 0;
    int top_down = 0;

    if (hdr_sz == 12) {
        w = r16(data, 18);
        h = r16(data, 20);
        bpp = r16(data, 24);
    } else if (hdr_sz >= 40) {
        w = r32s(data, 18);
        h = r32s(data, 22);
        bpp = r16(data, 28);
        compression = r32(data, 30);
        palette_colors = (int)r32(data, 46);
    } else {
        return 0;
    }

    if (w <= 0 || w > 4096) return 0;
    if (h == 0 || h > 4096 || h < -4096) return 0;
    if (h < 0) { top_down = 1; h = -h; }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return 0;
    if (compression > 3) return 0;
    if (bpp >= 16 && compression == 1) return 0;
    if (bpp == 16 && compression > 3) return 0;
    if (bpp == 1 && compression > 0) return 0;

    if (pix_off < 14 + hdr_sz || pix_off >= (uint32_t)len) return 0;

    // Palette size in file
    int pal_size = 0;
    img->palette_size = 0;
    if (bpp <= 8) {
        int max_colors = (bpp == 1) ? 2 : (bpp == 4) ? 16 : 256;
        int pal_entries = (palette_colors > 0 && palette_colors <= max_colors) ? palette_colors : max_colors;
        int entry_size = (hdr_sz == 12) ? 3 : 4;
        pal_size = pal_entries * entry_size;
        if (pix_off < 14 + hdr_sz + pal_size) return 0;
    }

    // Read bitfield masks for 16-bit
    uint32_t rm = 0, gm = 0, bm = 0;
    int r_shift = 0, g_shift = 0, b_shift = 0;
    int r_bits = 0, g_bits = 0, b_bits = 0;
    if (bpp == 16) {
        if (compression == 3 && hdr_sz >= 56) {
            rm = r32(data, 54);
            gm = r32(data, 58);
            bm = r32(data, 62);
        } else if (compression == 0) {
            rm = 0x7C00; gm = 0x03E0; bm = 0x001F;
        }
        if (rm) { r_shift = __builtin_ctz(rm); r_bits = __builtin_popcount(rm >> r_shift); }
        if (gm) { g_shift = __builtin_ctz(gm); g_bits = __builtin_popcount(gm >> g_shift); }
        if (bm) { b_shift = __builtin_ctz(bm); b_bits = __builtin_popcount(bm >> b_shift); }
    }

    // Read palette
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = 0;
    if (bpp <= 8) {
        int max_colors = (bpp == 1) ? 2 : (bpp == 4) ? 16 : 256;
        int pal_entries = (palette_colors > 0 && palette_colors <= max_colors) ? palette_colors : max_colors;
        int entry_size = (hdr_sz == 12) ? 3 : 4;
        for (int i = 0; i < pal_entries; i++) {
            int off = 14 + hdr_sz + i * entry_size;
            if (off + 2 >= len) break;
            uint32_t b = data[off];
            uint32_t g = data[off + 1];
            uint32_t r = data[off + 2];
            pal[i] = (r << 16) | (g << 8) | b;
        }
        // Store palette in img for later encode
        img->palette_size = pal_entries;
        for (int i = 0; i < pal_entries; i++) img->palette[i] = pal[i];
    }

    int row_size = ((w * (int)bpp + 31) / 32) * 4;
    /* the pixel data must actually be present — reject truncated files so we
     * never decode garbage read past the buffer (RLE paths are bounded by
     * their own src < len guards) */
    if (compression == 0 && (uint64_t)pix_off + (uint64_t)row_size * h > (uint64_t)len)
        return 0;
    uint8_t* out = (uint8_t*)kmalloc((size_t)(w * h * 3));
    if (!out) return 0;

    // RLE8 decompression buffer
    uint8_t* rle_buf = 0;
    if ((compression == 1 && bpp == 8) || (compression == 2 && bpp == 4)) {
        rle_buf = (uint8_t*)kmalloc((size_t)(w * h));
        if (!rle_buf) { kfree(out); return 0; }
        for (int i = 0; i < w * h; i++) rle_buf[i] = 0;
        int src = (int)pix_off;
        int rx = 0, ry = 0;
        while (ry < h && src + 1 < len) {
            uint8_t b1 = data[src++];
            uint8_t b2 = data[src++];
            if (b1 == 0) {
                if (b2 == 0) { ry++; rx = 0; }
                else if (b2 == 1) break;
                else if (b2 == 2) {
                    if (src + 1 >= len) break;
                    rx += data[src++];
                    ry += data[src++];
                } else {
                    int abs_len = b2;
                    for (int k = 0; k < abs_len; k++) {
                        if (rx < w && ry < h) {
                            if (compression == 1) rle_buf[ry * w + rx] = data[src];
                            else { if (k & 1) rle_buf[ry * w + rx] = data[src] & 0x0F; else rle_buf[ry * w + rx] = (data[src] >> 4) & 0x0F; }
                        }
                        if (compression == 1) { src++; if (k & 1 && k == abs_len - 1) src++; }
                        else { if (k & 1) src++; }
                        rx++;
                    }
                    if (compression == 1 && (abs_len & 1)) src++;
                    if (compression == 2 && ((abs_len + 1) / 2) & 1) src++;
                }
            } else {
                for (int k = 0; k < b1; k++) {
                    if (rx < w && ry < h) {
                        if (compression == 1) rle_buf[ry * w + rx] = b2;
                        else { if (k & 1) rle_buf[ry * w + rx] = b2 & 0x0F; else rle_buf[ry * w + rx] = (b2 >> 4) & 0x0F; }
                    }
                    rx++;
                }
            }
        }
    }

    for (int y = 0; y < h; y++) {
        int src_y = top_down ? y : (h - 1 - y);
        int src_off = (int)pix_off + src_y * row_size;
        uint8_t* dst = out + y * w * 3;

        if ((compression == 1 || compression == 2) && rle_buf) {
            for (int x = 0; x < w; x++) {
                uint32_t c = pal[rle_buf[y * w + x]];
                dst[x * 3 + 0] = (c >> 16) & 0xFF;
                dst[x * 3 + 1] = (c >> 8) & 0xFF;
                dst[x * 3 + 2] = c & 0xFF;
            }
            continue;
        }

        for (int x = 0; x < w; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (bpp == 24) {
                int p = src_off + x * 3;
                if (p + 2 < len) { b = data[p]; g = data[p+1]; r = data[p+2]; }
            } else if (bpp == 32) {
                int p = src_off + x * 4;
                if (p + 2 < len) { b = data[p]; g = data[p+1]; r = data[p+2]; }
            } else if (bpp == 16) {
                int p = src_off + x * 2;
                if (p + 1 < len) {
                    uint16_t v = r16(data, p);
                    if (rm) {
                        int rv = (v & rm) >> r_shift;
                        int gv = (v & gm) >> g_shift;
                        int bv = (v & bm) >> b_shift;
                        r = rep_bits(rv, r_bits);
                        g = rep_bits(gv, g_bits);
                        b = rep_bits(bv, b_bits);
                    } else {
                        int rv = (v >> 10) & 0x1F;
                        int gv = (v >> 5) & 0x1F;
                        int bv = v & 0x1F;
                        r = rep_bits(rv, 5);
                        g = rep_bits(gv, 5);
                        b = rep_bits(bv, 5);
                    }
                }
            } else if (bpp <= 8) {
                int byte_off = src_off + (x * (int)bpp) / 8;
                int bit_off = (8 - (int)bpp) - (x * (int)bpp) % 8;
                if (byte_off < len) {
                    int idx = 0;
                    if (bpp == 8) idx = data[byte_off];
                    else if (bpp == 4) idx = (data[byte_off] >> bit_off) & 0x0F;
                    else if (bpp == 1) idx = (data[byte_off] >> bit_off) & 0x01;
                    uint32_t c = pal[idx];
                    r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF;
                }
            }
            dst[x * 3] = r; dst[x * 3 + 1] = g; dst[x * 3 + 2] = b;
        }
    }

    if (rle_buf) kfree(rle_buf);

    img->width = w;
    img->height = h;
    img->bpp = (int)bpp;
    img->pixels = out;
    return 1;
}

void bmp_free(bmp_image_t* img) {
    if (img) {
        if (img->pixels) { kfree(img->pixels); img->pixels = 0; }
        img->palette_size = 0;
    }
}

// ── Simple median-cut quantizer for 24→8 bit ─────────────────
typedef struct { int r, g, b; int count; } bmp_color_t;

static int quantize_median_cut(const uint8_t* rgb, int w, int h, uint32_t pal[256], int max_cols) {
    int total = w * h;
    if (total <= 0 || max_cols > 256) max_cols = 256;

    // Build frequency table (quantized to 6-bit for speed).
    // NOTE: 64*64*64 ints = 1 MiB — far too big for the kernel stack, so it
    // lives on the heap instead.
    #define QS 64
    #define QM 63
    int* freq = (int*)kmalloc(sizeof(int) * QS * QS * QS);
    if (!freq) return 0;
    for (int i = 0; i < QS * QS * QS; i++) freq[i] = 0;
    for (int i = 0; i < total; i++) {
        int ri = (rgb[i*3] >> 2) & QM;
        int gi = (rgb[i*3+1] >> 2) & QM;
        int bi = (rgb[i*3+2] >> 2) & QM;
        freq[(ri*QS + gi)*QS + bi]++;
    }

    // Collect non-empty boxes (cap at the palette size we actually need).
    bmp_color_t cols[256];
    int col_count = 0;
    for (int ri = 0; ri < QS && col_count < 256; ri++) {
        for (int gi = 0; gi < QS && col_count < 256; gi++) {
            for (int bi = 0; bi < QS && col_count < 256; bi++) {
                int c = freq[(ri*QS + gi)*QS + bi];
                if (c > 0) {
                    cols[col_count].r = ri * 4 + 2;
                    cols[col_count].g = gi * 4 + 2;
                    cols[col_count].b = bi * 4 + 2;
                    cols[col_count].count = c;
                    col_count++;
                }
            }
        }
    }
    kfree(freq);
    #undef QS
    #undef QM

    if (col_count == 0) {
        pal[0] = 0; return 1;
    }

    // Simple popularity: use most frequent colors
    // Sort by frequency (bubble sort top max_cols)
    for (int i = 0; i < col_count && i < max_cols; i++) {
        int best = i;
        for (int j = i + 1; j < col_count; j++) {
            if (cols[j].count > cols[best].count) best = j;
        }
        bmp_color_t t = cols[i]; cols[i] = cols[best]; cols[best] = t;
    }
    if (col_count > max_cols) col_count = max_cols;

    for (int i = 0; i < col_count; i++) {
        pal[i] = (cols[i].r << 16) | (cols[i].g << 8) | cols[i].b;
    }
    return col_count;
}

// ── Write BMP header ─────────────────────────────────────────
static uint8_t* bmp_write_header(int w, int h, int bpp, int pal_size, int* out_off) {
    int row_bytes = ((w * bpp + 31) / 32) * 4;
    int pixel_data_size = row_bytes * h;
    int file_size = 14 + 40 + pal_size + pixel_data_size;
    uint8_t* buf = (uint8_t*)kmalloc(file_size);
    if (!buf) { *out_off = 0; return 0; }

    int off = 0;
    buf[off++] = 'B'; buf[off++] = 'M';
    buf[off++] = file_size & 0xFF;
    buf[off++] = (file_size >> 8) & 0xFF;
    buf[off++] = (file_size >> 16) & 0xFF;
    buf[off++] = (file_size >> 24) & 0xFF;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = (14 + 40 + pal_size) & 0xFF;
    buf[off++] = ((14 + 40 + pal_size) >> 8) & 0xFF;
    buf[off++] = ((14 + 40 + pal_size) >> 16) & 0xFF;
    buf[off++] = ((14 + 40 + pal_size) >> 24) & 0xFF;

    buf[off++] = 40; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = w & 0xFF; buf[off++] = (w >> 8) & 0xFF; buf[off++] = (w >> 16) & 0xFF; buf[off++] = (w >> 24) & 0xFF;
    buf[off++] = h & 0xFF; buf[off++] = (h >> 8) & 0xFF; buf[off++] = (h >> 16) & 0xFF; buf[off++] = (h >> 24) & 0xFF;
    buf[off++] = 1; buf[off++] = 0;
    buf[off++] = bpp; buf[off++] = 0;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;

    *out_off = off;
    return buf;
}

// ── Nearest palette color (Euclidean distance) ───────────────
static int nearest_pal(const uint32_t* pal, int n, int r, int g, int b) {
    int best = 0, best_dist = 0x7FFFFFFF;
    for (int i = 0; i < n; i++) {
        int pr = (pal[i] >> 16) & 0xFF;
        int pg = (pal[i] >> 8) & 0xFF;
        int pb = pal[i] & 0xFF;
        int dr = r - pr, dg = g - pg, db = b - pb;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) { best_dist = dist; best = i; }
    }
    return best;
}

// ── Encode ───────────────────────────────────────────────────
int bmp_encode(const bmp_image_t* img, uint8_t** out_data, int* out_len) {
    if (!img || !img->pixels || !out_data || !out_len) return 0;
    int w = img->width;
    int h = img->height;

    // Use palette if available, otherwise output 24-bit
    int out_bpp = 24;
    int pal_size = 0;
    uint32_t enc_pal[256];
    int enc_pal_count = 0;

    if (img->palette_size > 0 && img->palette_size <= 256) {
        // Use existing palette from decoded image
        out_bpp = (img->palette_size <= 2) ? 1 : (img->palette_size <= 16) ? 4 : 8;
        enc_pal_count = img->palette_size;
        for (int i = 0; i < enc_pal_count; i++) enc_pal[i] = img->palette[i];
    } else {
        // Quantize 24-bit to 8-bit palette for smaller files
        if (w * h <= 262144 && w * h > 0) {
            enc_pal_count = quantize_median_cut(img->pixels, w, h, enc_pal, 256);
            if (enc_pal_count >= 2 && enc_pal_count <= 256) {
                out_bpp = (enc_pal_count <= 2) ? 1 : (enc_pal_count <= 16) ? 4 : 8;
            } else {
                enc_pal_count = 0;
            }
        }
    }

    if (enc_pal_count > 0) {
        pal_size = enc_pal_count * 4;
    }

    int off = 0;
    uint8_t* buf = bmp_write_header(w, h, out_bpp, pal_size, &off);
    if (!buf) return 0;

    // Write palette
    if (pal_size > 0) {
        for (int i = 0; i < enc_pal_count; i++) {
            buf[off++] = enc_pal[i] & 0xFF;
            buf[off++] = (enc_pal[i] >> 8) & 0xFF;
            buf[off++] = (enc_pal[i] >> 16) & 0xFF;
            buf[off++] = 0;
        }
    }

    int row_size = ((w * out_bpp + 31) / 32) * 4;
    for (int y = h - 1; y >= 0; y--) {
        int row_start = off;
        if (out_bpp == 24) {
            for (int x = 0; x < w; x++) {
                const uint8_t* p = img->pixels + (y * w + x) * 3;
                buf[off++] = p[2];
                buf[off++] = p[1];
                buf[off++] = p[0];
            }
        } else if (out_bpp == 8) {
            for (int x = 0; x < w; x++) {
                const uint8_t* p = img->pixels + (y * w + x) * 3;
                buf[off++] = (uint8_t)nearest_pal(enc_pal, enc_pal_count, p[0], p[1], p[2]);
            }
        } else if (out_bpp == 4) {
            for (int x = 0; x < w; x += 2) {
                const uint8_t* p1 = img->pixels + (y * w + x) * 3;
                int i1 = nearest_pal(enc_pal, enc_pal_count, p1[0], p1[1], p1[2]);
                int i2 = 0;
                if (x + 1 < w) {
                    const uint8_t* p2 = img->pixels + (y * w + x + 1) * 3;
                    i2 = nearest_pal(enc_pal, enc_pal_count, p2[0], p2[1], p2[2]);
                }
                buf[off++] = (uint8_t)((i1 << 4) | i2);
            }
        } else if (out_bpp == 1) {
            for (int x = 0; x < w; x += 8) {
                uint8_t byte = 0;
                for (int k = 0; k < 8; k++) {
                    if (x + k < w) {
                        const uint8_t* p = img->pixels + (y * w + x + k) * 3;
                        int idx = nearest_pal(enc_pal, enc_pal_count, p[0], p[1], p[2]);
                        if (idx) byte |= (1 << (7 - k));
                    }
                }
                buf[off++] = byte;
            }
        }
        while (off - row_start < row_size) buf[off++] = 0;
    }

    *out_data = buf;
    *out_len = off;
    return 1;
}
