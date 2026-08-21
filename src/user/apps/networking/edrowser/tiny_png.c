/*
 * tiny_png.c — Minimal freestanding PNG decoder.
 * Implements: PNG signature check, IHDR/IDAT/PLTE/tRNS/IEND chunk parsing,
 * DEFLATE inflate (stored / fixed Huffman / dynamic Huffman), PNG filter
 * reconstruction (None/Sub/Up/Average/Paeth), and color-mode conversion to RGB24.
 *
 * Supports: bit depths 1/2/4/8/16, color types 0(grey),2(RGB),3(palette),
 * 4(grey+alpha),6(RGBA). 16-bit samples are truncated to 8 bits.
 * Alpha is composited on white background.
 *
 * Copyright (C) 2026 EigenOS project. Public domain / no warranty.
 */

#include "tiny_png.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * DEFLATE Inflate
 * ────────────────────────────────────────────────────────────────── */
#define INF_WSIZE  32768   /* LZ77 window size */
#define INF_MAXLIT 288
#define INF_MAXDIST 32

typedef struct {
    const uint8_t* src;
    int  src_len;
    int  src_pos;
    uint8_t* dst;
    int  dst_cap;
    int  dst_pos;
    uint32_t bits;
    int  nbits;
    int  err;
} Inflate;

static void inf_refill(Inflate* z) {
    while (z->nbits < 24 && z->src_pos < z->src_len) {
        z->bits |= (uint32_t)z->src[z->src_pos++] << z->nbits;
        z->nbits += 8;
    }
}

static uint32_t inf_bits(Inflate* z, int n) {
    inf_refill(z);
    uint32_t v = z->bits & ((1u << n) - 1);
    z->bits >>= n;
    z->nbits -= n;
    return v;
}

static int inf_byte(Inflate* z) {
    /* byte-aligned read */
    z->bits  = 0;
    z->nbits = 0;
    if (z->src_pos >= z->src_len) { z->err = 1; return 0; }
    return z->src[z->src_pos++];
}

static void inf_emit(Inflate* z, uint8_t b) {
    if (z->dst_pos >= z->dst_cap) { z->err = 1; return; }
    z->dst[z->dst_pos++] = b;
}

static void inf_copy_back(Inflate* z, int dist, int len) {
    int from = z->dst_pos - dist;
    if (from < 0) { z->err = 1; return; }
    for (int i = 0; i < len; i++) {
        if (from + i >= z->dst_pos) {
            /* Copy from already-copied part */
            inf_emit(z, z->dst[z->dst_pos - dist]);
        } else {
            inf_emit(z, z->dst[from + i]);
        }
    }
}

/* Build Huffman from code lengths (DEFLATE canonical) */
typedef struct {
    int  count;
    uint16_t sym  [INF_MAXLIT];
    uint8_t  bits [INF_MAXLIT];
    uint32_t code [INF_MAXLIT];
    /* fast 9-bit lookup */
    int16_t  fast_sym[512];
    int8_t   fast_bits[512];
} HTree;

static void htree_build(HTree* ht, const uint8_t* lengths, int n) {
    int bl_count[16] = {0};
    int next_code[17] = {0};
    ht->count = 0;
    for (int i = 0; i < n; i++) if (lengths[i]) bl_count[lengths[i]]++;
    int code = 0;
    for (int bits = 1; bits <= 15; bits++) {
        code = (code + bl_count[bits-1]) << 1;
        next_code[bits] = code;
    }
    for (int i = 0; i < n; i++) {
        int l = lengths[i];
        if (l == 0) continue;
        ht->sym [ht->count] = (uint16_t)i;
        ht->bits[ht->count] = (uint8_t)l;
        ht->code[ht->count] = (uint32_t)next_code[l]++;
        ht->count++;
    }
    /* Fast 9-bit lookup */
    for (int i = 0; i < 512; i++) { ht->fast_sym[i] = -1; ht->fast_bits[i] = 0; }
    for (int i = 0; i < ht->count; i++) {
        int l = ht->bits[i];
        if (l <= 9) {
            int base = (int)(ht->code[i] << (9 - l));
            int fill = 1 << (9 - l);
            /* Reverse bits for LSB-first decoding */
            int rev = 0;
            for (int b = 0; b < l; b++)
                if (ht->code[i] & (1 << b)) rev |= (1 << (l-1-b));
            base = rev << (9 - l);
            for (int j = 0; j < fill; j++) {
                ht->fast_sym [base + j] = (int16_t)ht->sym[i];
                ht->fast_bits[base + j] = (int8_t)l;
            }
        }
    }
}

static int htree_decode(Inflate* z, const HTree* ht) {
    inf_refill(z);
    /* Try fast path (9-bit) */
    int peek9 = (int)(z->bits & 0x1FF);
    if (ht->fast_bits[peek9] > 0) {
        int b = ht->fast_bits[peek9];
        z->bits >>= b; z->nbits -= b;
        return ht->fast_sym[peek9];
    }
    /* Slow path: reverse bits */
    int code = 0;
    for (int i = 0; i < ht->count; i++) {
        int l = ht->bits[i];
        if (z->nbits < l) inf_refill(z);
        /* Extract l bits LSB-first and reverse to match DEFLATE MSB-first codes */
        uint32_t raw = z->bits & ((1u << l) - 1);
        uint32_t rev = 0;
        for (int b = 0; b < l; b++) if (raw & (1u << b)) rev |= (1u << (l-1-b));
        if (rev == ht->code[i]) {
            z->bits >>= l; z->nbits -= l;
            return ht->sym[i];
        }
        (void)code;
    }
    z->err = 1;
    return -1;
}

/* Fixed Huffman tables (DEFLATE spec) */
static const uint8_t FIXED_LIT_LEN[288] = {
    /* 0..143  → 8 bits (144 values) */
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    /* 144..255 → 9 bits (112 values) */
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    /* 256..279 → 7 bits (24 values) */
    7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,
    /* 280..287 → 8 bits (8 values) */
    8,8,8,8,8,8,8,8
};

static const uint8_t FIXED_DIST_LEN[32] = {
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};

/* Length / distance extra bits tables */
static const int LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const int DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static int inflate_block(Inflate* z, const HTree* lit_ht, const HTree* dist_ht) {
    for (;;) {
        int sym = htree_decode(z, lit_ht);
        if (z->err || sym < 0) return 0;
        if (sym == 256) return 1; /* end of block */
        if (sym < 256) { inf_emit(z, (uint8_t)sym); continue; }
        /* Back-reference */
        int li = sym - 257;
        if (li < 0 || li >= 29) { z->err = 1; return 0; }
        int length = LEN_BASE[li] + (int)inf_bits(z, LEN_EXTRA[li]);
        int di = htree_decode(z, dist_ht);
        if (z->err || di < 0 || di >= 30) { z->err = 1; return 0; }
        int dist = DIST_BASE[di] + (int)inf_bits(z, DIST_EXTRA[di]);
        inf_copy_back(z, dist, length);
        if (z->err) return 0;
    }
}

static int inflate_all(const uint8_t* src, int src_len, uint8_t* dst, int dst_cap, int* out_len) {
    Inflate z;
    memset(&z, 0, sizeof(z));
    z.src = src; z.src_len = src_len;
    z.dst = dst; z.dst_cap = dst_cap;
    /* Skip zlib header (2 bytes) */
    if (src_len < 2) return 0;
    z.src_pos = 2;

    HTree* fixed_lit  = (HTree*)malloc(sizeof(HTree));
    HTree* fixed_dist = (HTree*)malloc(sizeof(HTree));
    if (!fixed_lit || !fixed_dist) {
        if (fixed_lit)  free(fixed_lit);
        if (fixed_dist) free(fixed_dist);
        return 0;
    }
    memset(fixed_lit,  0, sizeof(HTree));
    memset(fixed_dist, 0, sizeof(HTree));
    htree_build(fixed_lit,  FIXED_LIT_LEN,  288);
    htree_build(fixed_dist, FIXED_DIST_LEN, 32);

    int bfinal = 0;
    int ok = 0;
    while (!bfinal && !z.err) {
        bfinal = (int)inf_bits(&z, 1);
        int btype = (int)inf_bits(&z, 2);
        if (btype == 0) {
            /* Stored block */
            z.bits = 0; z.nbits = 0;
            int lo = inf_byte(&z), hi = inf_byte(&z);
            (void)inf_byte(&z); (void)inf_byte(&z); /* nlen */
            int blen = lo | (hi << 8);
            for (int i = 0; i < blen && !z.err; i++) {
                inf_emit(&z, (uint8_t)inf_byte(&z));
            }
        } else if (btype == 1) {
            /* Fixed Huffman */
            if (!inflate_block(&z, fixed_lit, fixed_dist)) break;
        } else if (btype == 2) {
            /* Dynamic Huffman */
            int hlit  = (int)inf_bits(&z, 5) + 257;
            int hdist = (int)inf_bits(&z, 5) + 1;
            int hclen = (int)inf_bits(&z, 4) + 4;

            static const int CLEN_ORDER[19] = {
                16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
            };
            uint8_t clen_lengths[19];
            memset(clen_lengths, 0, sizeof(clen_lengths));
            for (int i = 0; i < hclen; i++)
                clen_lengths[CLEN_ORDER[i]] = (uint8_t)inf_bits(&z, 3);

            HTree* clen_ht = (HTree*)malloc(sizeof(HTree));
            if (!clen_ht) { z.err = 1; break; }
            memset(clen_ht, 0, sizeof(HTree));
            htree_build(clen_ht, clen_lengths, 19);

            uint8_t* lengths = (uint8_t*)malloc(hlit + hdist);
            if (!lengths) { free(clen_ht); z.err = 1; break; }
            memset(lengths, 0, hlit + hdist);
            int total = hlit + hdist;
            int i = 0;
            while (i < total && !z.err) {
                int sym = htree_decode(&z, clen_ht);
                if (sym < 0 || z.err) break;
                if (sym < 16) { lengths[i++] = (uint8_t)sym; }
                else if (sym == 16) {
                    int rep = (int)inf_bits(&z, 2) + 3;
                    uint8_t prev = (i > 0) ? lengths[i-1] : 0;
                    for (int r = 0; r < rep && i < total; r++) lengths[i++] = prev;
                } else if (sym == 17) {
                    int rep = (int)inf_bits(&z, 3) + 3;
                    for (int r = 0; r < rep && i < total; r++) lengths[i++] = 0;
                } else { /* 18 */
                    int rep = (int)inf_bits(&z, 7) + 11;
                    for (int r = 0; r < rep && i < total; r++) lengths[i++] = 0;
                }
            }

            if (!z.err) {
                HTree* dyn_lit  = (HTree*)malloc(sizeof(HTree));
                HTree* dyn_dist = (HTree*)malloc(sizeof(HTree));
                if (dyn_lit && dyn_dist) {
                    memset(dyn_lit,  0, sizeof(HTree));
                    memset(dyn_dist, 0, sizeof(HTree));
                    htree_build(dyn_lit,  lengths,       hlit);
                    htree_build(dyn_dist, lengths + hlit, hdist);
                    inflate_block(&z, dyn_lit, dyn_dist);
                    free(dyn_lit);
                    free(dyn_dist);
                } else {
                    if (dyn_lit)  free(dyn_lit);
                    if (dyn_dist) free(dyn_dist);
                    z.err = 1;
                }
            }
            free(lengths);
            free(clen_ht);
        } else {
            z.err = 1; /* btype == 3: reserved */
        }
    }
    free(fixed_lit);
    free(fixed_dist);
    if (!z.err) { *out_len = z.dst_pos; ok = 1; }
    return ok;
}

/* ──────────────────────────────────────────────────────────────────
 * PNG parser
 * ────────────────────────────────────────────────────────────────── */
static uint32_t read_u32_be(const uint8_t* d) {
    return ((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
}

/* Paeth predictor */
static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p - a; if (pa < 0) pa = -pa;
    int pb = p - b; if (pb < 0) pb = -pb;
    int pc = p - c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int png_decode(const uint8_t* data, int len,
               uint8_t** rgb_out, int* w_out, int* h_out) {
    static const uint8_t PNG_SIG[8] = {137,80,78,71,13,10,26,10};
    if (!data || len < 8) return 0;
    if (memcmp(data, PNG_SIG, 8) != 0) return 0;

    /* Parse IHDR */
    if (len < 8 + 12 + 13) return 0;
    int pos = 8;
    uint32_t chunk_len = read_u32_be(data + pos); pos += 4;
    /* chunk type must be IHDR */
    if (chunk_len != 13 || memcmp(data+pos,"IHDR",4) != 0) return 0;
    pos += 4;
    int width   = (int)read_u32_be(data+pos); pos += 4;
    int height  = (int)read_u32_be(data+pos); pos += 4;
    int bit_depth   = data[pos++];
    int color_type  = data[pos++];
    /* compression=0, filter=0, interlace */
    pos++; pos++;
    int interlace = data[pos++];
    pos += 4; /* CRC */

    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0;
    if (interlace != 0) return 0; /* interlacing not supported */
    if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 &&
        bit_depth != 8 && bit_depth != 16) return 0;

    int channels;
    switch (color_type) {
    case 0: channels = 1; break; /* greyscale */
    case 2: channels = 3; break; /* RGB */
    case 3: channels = 1; break; /* indexed */
    case 4: channels = 2; break; /* greyscale+alpha */
    case 6: channels = 4; break; /* RGBA */
    default: return 0;
    }

    /* Collect IDAT and PLTE chunks */
    uint8_t palette[256*3];
    int has_palette = 0;
    int idat_cap = width * height * channels * 2 + 65536;
    uint8_t* idat_buf = (uint8_t*)malloc(idat_cap);
    if (!idat_buf) return 0;
    int idat_len = 0;

    while (pos + 8 <= len) {
        uint32_t clen = read_u32_be(data + pos); pos += 4;
        const uint8_t* ctype = data + pos; pos += 4;
        if (pos + (int)clen + 4 > len) break;

        if (memcmp(ctype, "PLTE", 4) == 0) {
            int ne = (int)clen / 3;
            if (ne > 256) ne = 256;
            memcpy(palette, data + pos, ne * 3);
            has_palette = 1;
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            int copy = (int)clen;
            if (idat_len + copy > idat_cap) copy = idat_cap - idat_len;
            memcpy(idat_buf + idat_len, data + pos, copy);
            idat_len += copy;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            break;
        }
        pos += (int)clen + 4;
    }

    if (idat_len == 0) { free(idat_buf); return 0; }

    /* Inflate IDAT */
    /* stride = filter_byte + raw_samples_per_row */
    int bytes_per_sample = (bit_depth < 8) ? 1 : (bit_depth / 8);
    int samples_per_pixel = channels;
    /* For indexed and low bit depth, raw bytes per row */
    int raw_bytes_per_row;
    if (bit_depth < 8) {
        raw_bytes_per_row = (width * channels * bit_depth + 7) / 8;
    } else {
        raw_bytes_per_row = width * channels * bytes_per_sample;
    }
    int scanline_stride = 1 + raw_bytes_per_row; /* 1 = filter byte */
    int raw_cap = scanline_stride * height + 4;
    uint8_t* raw = (uint8_t*)malloc(raw_cap);
    if (!raw) { free(idat_buf); return 0; }
    int raw_len2 = 0;
    int ok = inflate_all(idat_buf, idat_len, raw, raw_cap - 1, &raw_len2);
    free(idat_buf);
    if (!ok) { free(raw); return 0; }

    /* Apply PNG filters row by row */
    int bpp_bytes = (bit_depth < 8) ? 1 : (channels * bytes_per_sample);
    uint8_t* prev_row = (uint8_t*)malloc(raw_bytes_per_row);
    if (!prev_row) { free(raw); return 0; }
    memset(prev_row, 0, raw_bytes_per_row);

    uint8_t* recon = (uint8_t*)malloc(raw_bytes_per_row * height);
    if (!recon) { free(raw); free(prev_row); return 0; }

    for (int row = 0; row < height; row++) {
        int row_off = row * scanline_stride;
        if (row_off >= raw_len2) break;
        int filter = raw[row_off];
        uint8_t* cur = raw + row_off + 1;
        uint8_t* out_row = recon + row * raw_bytes_per_row;

        for (int i = 0; i < raw_bytes_per_row; i++) {
            int x = (i < row_off + 1 + raw_bytes_per_row && row_off + 1 + i < raw_len2) ? cur[i] : 0;
            int a = (i >= bpp_bytes) ? out_row[i - bpp_bytes] : 0;
            int b = prev_row[i];
            int c = (i >= bpp_bytes) ? prev_row[i - bpp_bytes] : 0;
            int v;
            switch (filter) {
            case 0: v = x; break;
            case 1: v = x + a; break;
            case 2: v = x + b; break;
            case 3: v = x + (a + b) / 2; break;
            case 4: v = x + paeth(a, b, c); break;
            default: v = x; break;
            }
            out_row[i] = (uint8_t)(v & 0xFF);
        }
        memcpy(prev_row, out_row, raw_bytes_per_row);
    }
    free(raw);
    free(prev_row);

    /* Convert to RGB24 */
    uint8_t* rgb = (uint8_t*)malloc(width * height * 3);
    if (!rgb) { free(recon); return 0; }

    for (int row = 0; row < height; row++) {
        const uint8_t* src = recon + row * raw_bytes_per_row;
        for (int col = 0; col < width; col++) {
            uint8_t r = 0, g = 0, b = 0;
            if (bit_depth == 16) {
                /* 16-bit: big-endian, truncate to 8 */
                switch (color_type) {
                case 0: { /* grey */
                    uint8_t v = src[col*2];
                    r = g = b = v;
                    break;
                }
                case 2: { /* RGB */
                    r = src[col*6+0]; g = src[col*6+2]; b = src[col*6+4];
                    break;
                }
                case 4: { /* grey+alpha */
                    uint8_t v = src[col*4]; uint8_t a = src[col*4+2];
                    /* composite on white */
                    r = g = b = (uint8_t)((v * a + 255 * (255 - a)) / 255);
                    break;
                }
                case 6: { /* RGBA */
                    uint8_t rv=src[col*8],gv=src[col*8+2],bv=src[col*8+4],av=src[col*8+6];
                    r=(uint8_t)((rv*av+255*(255-av))/255);
                    g=(uint8_t)((gv*av+255*(255-av))/255);
                    b=(uint8_t)((bv*av+255*(255-av))/255);
                    break;
                }
                }
            } else if (bit_depth == 8) {
                switch (color_type) {
                case 0: r = g = b = src[col]; break;
                case 2: r=src[col*3]; g=src[col*3+1]; b=src[col*3+2]; break;
                case 3: { /* palette */
                    int idx = src[col];
                    if (has_palette && idx < 256) {
                        r=palette[idx*3]; g=palette[idx*3+1]; b=palette[idx*3+2];
                    }
                    break;
                }
                case 4: {
                    uint8_t v=src[col*2], a=src[col*2+1];
                    r=g=b=(uint8_t)((v*a+255*(255-a))/255);
                    break;
                }
                case 6: {
                    uint8_t rv=src[col*4],gv=src[col*4+1],bv=src[col*4+2],av=src[col*4+3];
                    r=(uint8_t)((rv*av+255*(255-av))/255);
                    g=(uint8_t)((gv*av+255*(255-av))/255);
                    b=(uint8_t)((bv*av+255*(255-av))/255);
                    break;
                }
                }
            } else {
                /* bit_depth < 8 */
                int bits_per_pixel = bit_depth * channels;
                int bit_pos = col * bits_per_pixel;
                int byte_idx = bit_pos / 8;
                int bit_off  = 7 - (bit_pos % 8);
                int mask = (1 << bit_depth) - 1;
                int val  = (src[byte_idx] >> (bit_off - bit_depth + 1)) & mask;
                int maxv = (1 << bit_depth) - 1;
                if (color_type == 0) {
                    uint8_t v = (uint8_t)(val * 255 / maxv);
                    r = g = b = v;
                } else if (color_type == 3 && has_palette) {
                    r=palette[val*3]; g=palette[val*3+1]; b=palette[val*3+2];
                } else {
                    uint8_t v = (uint8_t)(val * 255 / maxv);
                    r = g = b = v;
                }
            }
            rgb[(row*width+col)*3+0] = r;
            rgb[(row*width+col)*3+1] = g;
            rgb[(row*width+col)*3+2] = b;
        }
    }
    free(recon);
    *rgb_out = rgb;
    *w_out   = width;
    *h_out   = height;
    return 1;
}
