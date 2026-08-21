/*
 * tiny_jpeg.c — Minimal freestanding baseline JPEG (SOF0) decoder.
 * Supports: baseline DCT, 8-bit, 1-component (greyscale) and 3-component
 * YCbCr, chroma subsampling 4:4:4 / 4:2:2 / 4:2:0, standard Huffman tables,
 * restart markers. Integer-only (fixed-point). No FPU required.
 *
 * Copyright (C) 2026 EigenOS project. Public domain / no warranty.
 */

#include "tiny_jpeg.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ─── Bit-stream reader ────────────────────────────────────────── */
typedef struct {
    const uint8_t* data;
    int            len;
    int            pos;   /* byte index */
    uint32_t       bits;  /* bit buffer (up to 32 bits) */
    int            nbits; /* valid bits in buffer */
    int            eof;
} Bits;

static void bits_init(Bits* b, const uint8_t* data, int len, int pos) {
    b->data  = data;
    b->len   = len;
    b->pos   = pos;
    b->bits  = 0;
    b->nbits = 0;
    b->eof   = 0;
}

/* Refill at least 'need' bits from the byte stream (byte-stuffing aware). */
static void bits_refill(Bits* b, int need) {
    while (b->nbits < need && !b->eof) {
        if (b->pos >= b->len) { b->eof = 1; break; }
        uint8_t c = b->data[b->pos++];
        if (c == 0xFF) {
            if (b->pos >= b->len) { b->eof = 1; break; }
            uint8_t c2 = b->data[b->pos++];
            if (c2 == 0x00) c = 0xFF;        /* byte-stuffed FF */
            else if (c2 >= 0xD0 && c2 <= 0xD7) { /* RST marker: ignore */ c = 0; }
            else { b->pos -= 2; b->eof = 1; break; } /* other marker: stop */
        }
        b->bits = (b->bits << 8) | c;
        b->nbits += 8;
    }
}

static int bits_get(Bits* b, int n) {
    if (n == 0) return 0;
    bits_refill(b, n);
    if (b->nbits < n) return 0;
    b->nbits -= n;
    return (int)((b->bits >> b->nbits) & ((1u << n) - 1));
}

static int bits_peek(Bits* b, int n) {
    bits_refill(b, n);
    if (b->nbits < n) return -1;
    return (int)((b->bits >> (b->nbits - n)) & ((1u << n) - 1));
}

static void bits_skip(Bits* b, int n) {
    b->nbits -= n;
}

/* ─── Huffman table ────────────────────────────────────────────── */
#define HUFF_MAX_BITS  16
#define HUFF_MAX_CODES 256

typedef struct {
    int     count;                     /* total codes */
    int     code[HUFF_MAX_CODES];
    int     size[HUFF_MAX_CODES];
    uint8_t val [HUFF_MAX_CODES];
    /* Fast lookup: for codes <= 8 bits we use a 256-entry table */
    int8_t  fast_bits[256];            /* -1 = not fast, else size */
    uint8_t fast_val [256];
} HuffTable;

static int huff_build(HuffTable* ht, const uint8_t* lengths, const uint8_t* values) {
    /* Build canonical Huffman from JPEG DHT format:
       lengths[0..15] = count of codes with bits 1..16
       values = the symbols in canonical order */
    ht->count = 0;
    int code  = 0;
    int idx   = 0;
    for (int bits = 1; bits <= HUFF_MAX_BITS; bits++) {
        for (int j = 0; j < (int)lengths[bits-1]; j++) {
            if (ht->count >= HUFF_MAX_CODES) return 0;
            ht->code[ht->count] = code;
            ht->size[ht->count] = bits;
            ht->val [ht->count] = values[idx++];
            ht->count++;
            code++;
        }
        code <<= 1;
    }
    /* Build fast lookup table (8-bit prefix) */
    for (int i = 0; i < 256; i++) { ht->fast_bits[i] = -1; ht->fast_val[i] = 0; }
    for (int i = 0; i < ht->count; i++) {
        int bits = ht->size[i];
        if (bits <= 8) {
            int base = ht->code[i] << (8 - bits);
            int fill = 1 << (8 - bits);
            for (int j = 0; j < fill; j++) {
                ht->fast_bits[base + j] = (int8_t)bits;
                ht->fast_val [base + j] = ht->val[i];
            }
        }
    }
    return 1;
}

static int huff_decode(Bits* b, const HuffTable* ht) {
    bits_refill(b, 16);
    if (b->nbits >= 8) {
        int peek8 = (int)((b->bits >> (b->nbits - 8)) & 0xFF);
        int fb = ht->fast_bits[peek8];
        if (fb > 0) {
            b->nbits -= fb;
            return ht->fast_val[peek8];
        }
    }
    /* Slow path: walk all codes */
    int code = 0;
    for (int i = 0; i < ht->count; i++) {
        int n = ht->size[i];
        if (b->nbits < n) break;
        int peek = (int)((b->bits >> (b->nbits - n)) & ((1u << n) - 1));
        if (peek == ht->code[i]) {
            b->nbits -= n;
            return ht->val[i];
        }
        (void)code;
    }
    return -1; /* error */
}

/* ─── IDCT (AAN integer method, scaled) ───────────────────────── */
/* Fixed-point constants: scaled by 2048 */
#define W1  2841  /* 2048*sqrt(2)*cos(1*pi/16) */
#define W2  2676  /* 2048*sqrt(2)*cos(2*pi/16) */
#define W3  2408  /* 2048*sqrt(2)*cos(3*pi/16) */
#define W5  1609  /* 2048*sqrt(2)*cos(5*pi/16) */
#define W6  1108  /* 2048*sqrt(2)*cos(6*pi/16) */
#define W7   565  /* 2048*sqrt(2)*cos(7*pi/16) */

static void idct_row(int* blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!(blk[1]|blk[2]|blk[3]|blk[4]|blk[5]|blk[6]|blk[7])) {
        blk[0]=blk[1]=blk[2]=blk[3]=blk[4]=blk[5]=blk[6]=blk[7]=blk[0]<<3;
        return;
    }
    x0 = (blk[0]<<11) + 128; x1 = blk[4]<<11;
    x2 = blk[6]; x3 = blk[2];
    x4 = blk[1]; x5 = blk[7]; x6 = blk[5]; x7 = blk[3];
    x8 = W7*(x4+x5);
    x4 = x8 + (W1-W7)*x4; x5 = x8 - (W1+W7)*x5;
    x8 = W3*(x6+x7);
    x6 = x8 - (W3-W5)*x6; x7 = x8 - (W3+W5)*x7;
    x8 = x0+x1; x0 -= x1;
    x1 = W6*(x3+x2); x2 = x1-(W2+W6)*x2; x3 = x1+(W2-W6)*x3;
    x1 = x4+x6; x4 -= x6; x6 = x5+x7; x5 -= x7;
    x7 = x8+x3; x8 -= x3; x3 = x0+x2; x0 -= x2;
    x2 = (181*(x4+x5)+128)>>8; x4 = (181*(x4-x5)+128)>>8;
    blk[0]=(x7+x1)>>8; blk[1]=(x3+x2)>>8; blk[2]=(x0+x4)>>8; blk[3]=(x8+x6)>>8;
    blk[4]=(x8-x6)>>8; blk[5]=(x0-x4)>>8; blk[6]=(x3-x2)>>8; blk[7]=(x7-x1)>>8;
}

static void idct_col(int* blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!(blk[8]|blk[16]|blk[24]|blk[32]|blk[40]|blk[48]|blk[56])) {
        blk[0]=blk[8]=blk[16]=blk[24]=blk[32]=blk[40]=blk[48]=blk[56]=
            (blk[0]+32)>>6;
        return;
    }
    x0 = (blk[0]<<8)+8192; x1 = blk[32]<<8;
    x2 = blk[48]; x3 = blk[16];
    x4 = blk[8]; x5 = blk[56]; x6 = blk[40]; x7 = blk[24];
    x8 = W7*(x4+x5)+4; x4=(x8+(W1-W7)*x4)>>3; x5=(x8-(W1+W7)*x5)>>3;
    x8 = W3*(x6+x7)+4; x6=(x8-(W3-W5)*x6)>>3; x7=(x8-(W3+W5)*x7)>>3;
    x8 = x0+x1; x0 -= x1;
    x1 = W6*(x3+x2)+4; x2=(x1-(W2+W6)*x2)>>3; x3=(x1+(W2-W6)*x3)>>3;
    x1 = x4+x6; x4 -= x6; x6 = x5+x7; x5 -= x7;
    x7 = x8+x3; x8 -= x3; x3 = x0+x2; x0 -= x2;
    x2 = (181*(x4+x5)+128)>>8; x4 = (181*(x4-x5)+128)>>8;
    blk[0] =(x7+x1)>>14; blk[8] =(x3+x2)>>14; blk[16]=(x0+x4)>>14; blk[24]=(x8+x6)>>14;
    blk[32]=(x8-x6)>>14; blk[40]=(x0-x4)>>14; blk[48]=(x3-x2)>>14; blk[56]=(x7-x1)>>14;
}

static void idct_block(int blk[64], uint8_t* out, int stride) {
    for (int i = 0; i < 8; i++) idct_row(blk + i*8);
    for (int i = 0; i < 8; i++) idct_col(blk + i);
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int v = blk[r*8+c] + 128;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            out[r*stride+c] = (uint8_t)v;
        }
    }
}

/* ─── JPEG decoder state ───────────────────────────────────────── */
#define MAX_COMP     3
#define MAX_HTABLES  4  /* 2 DC + 2 AC (indexed 0..3) */
#define MAX_QTABLES  4

typedef struct {
    int id, h, v, qtbl, dc_htbl, ac_htbl;
    int dc_pred;
    /* Component plane */
    uint8_t* plane;
    int plane_w, plane_h;
} JComp;

typedef struct {
    int            width, height;
    int            ncomp;
    JComp          comp[MAX_COMP];
    HuffTable      dc_ht[2];
    HuffTable      ac_ht[2];
    int            quant[MAX_QTABLES][64];  /* dequantized, natural order */
    int            valid_dc[2], valid_ac[2];
    int            mcu_w, mcu_h;    /* MCU size in pixels */
    int            mcus_x, mcus_y;
    Bits           bits;
    const uint8_t* data;
    int            data_len;
} JDec;

/* Zigzag scan order (JPEG standard) */
static const uint8_t ZIG[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

/* Clip int to [0,255] */
static inline uint8_t clip8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* ─── Decode one MCU component block ──────────────────────────── */
static int decode_block(JDec* j, JComp* c, int dc_id, int ac_id) {
    int blk[64];
    memset(blk, 0, sizeof(blk));

    /* DC coefficient */
    int dc_sym = huff_decode(&j->bits, &j->dc_ht[dc_id]);
    if (dc_sym < 0) return 0;
    int dc_diff = 0;
    if (dc_sym > 0) {
        dc_diff = bits_get(&j->bits, dc_sym);
        if (dc_diff < (1 << (dc_sym - 1)))
            dc_diff -= (1 << dc_sym) - 1;
    }
    c->dc_pred += dc_diff;
    blk[0] = c->dc_pred * j->quant[c->qtbl][0];

    /* AC coefficients */
    int k = 1;
    while (k < 64) {
        int ac_sym = huff_decode(&j->bits, &j->ac_ht[ac_id]);
        if (ac_sym < 0) return 0;
        if (ac_sym == 0x00) break; /* EOB */
        int run  = (ac_sym >> 4) & 0xF;
        int size = (ac_sym     ) & 0xF;
        if (ac_sym == 0xF0) { k += 16; continue; } /* ZRL */
        k += run;
        if (k >= 64) break;
        int val = bits_get(&j->bits, size);
        if (val < (1 << (size - 1)))
            val -= (1 << size) - 1;
        blk[ZIG[k]] = val * j->quant[c->qtbl][ZIG[k]];
        k++;
    }
    return 1;
}

/* ─── JPEG parser helpers ──────────────────────────────────────── */
static uint16_t read_u16(const uint8_t* d) {
    return (uint16_t)((d[0] << 8) | d[1]);
}

static int parse_dht(JDec* j, const uint8_t* seg, int seg_len) {
    int p = 0;
    while (p < seg_len) {
        if (p >= seg_len) break;
        uint8_t tc_th = seg[p++];
        int tc = (tc_th >> 4) & 1;   /* 0=DC, 1=AC */
        int th = (tc_th     ) & 3;   /* table id 0..3 */
        if (th > 1) return 0;         /* only tables 0 and 1 used */

        uint8_t lengths[16];
        int total = 0;
        for (int i = 0; i < 16; i++) {
            lengths[i] = seg[p++];
            total += lengths[i];
        }
        if (p + total > seg_len || total > 256) return 0;
        const uint8_t* vals = seg + p;
        p += total;

        if (tc == 0) {
            huff_build(&j->dc_ht[th], lengths, vals);
            j->valid_dc[th] = 1;
        } else {
            huff_build(&j->ac_ht[th], lengths, vals);
            j->valid_ac[th] = 1;
        }
    }
    return 1;
}

static int parse_dqt(JDec* j, const uint8_t* seg, int seg_len) {
    int p = 0;
    while (p + 65 <= seg_len) {
        uint8_t prec_id = seg[p++];
        int prec = (prec_id >> 4);    /* 0=8-bit, 1=16-bit */
        int id   = (prec_id     ) & 3;
        if (id >= MAX_QTABLES) return 0;
        for (int i = 0; i < 64; i++) {
            int v;
            if (prec == 0) v = seg[p++];
            else { v = (seg[p] << 8) | seg[p+1]; p += 2; }
            j->quant[id][ZIG[i]] = v;
        }
    }
    return 1;
}

static int parse_sof0(JDec* j, const uint8_t* seg, int seg_len) {
    if (seg_len < 11) return 0;
    /* precision = seg[0], must be 8 */
    if (seg[0] != 8) return 0;
    j->height = read_u16(seg + 1);
    j->width  = read_u16(seg + 3);
    j->ncomp  = seg[5];
    if (j->ncomp != 1 && j->ncomp != 3) return 0;
    if (seg_len < 6 + j->ncomp * 3) return 0;
    int mcu_max_h = 1, mcu_max_v = 1;
    for (int i = 0; i < j->ncomp; i++) {
        j->comp[i].id   = seg[6 + i*3];
        uint8_t hv      = seg[7 + i*3];
        j->comp[i].h    = (hv >> 4) & 0xF;
        j->comp[i].v    = (hv     ) & 0xF;
        j->comp[i].qtbl = seg[8 + i*3] & 3;
        if (j->comp[i].h > mcu_max_h) mcu_max_h = j->comp[i].h;
        if (j->comp[i].v > mcu_max_v) mcu_max_v = j->comp[i].v;
    }
    j->mcu_w = mcu_max_h * 8;
    j->mcu_h = mcu_max_v * 8;
    j->mcus_x = (j->width  + j->mcu_w - 1) / j->mcu_w;
    j->mcus_y = (j->height + j->mcu_h - 1) / j->mcu_h;
    /* Assign default Huffman table mapping for 3-comp */
    if (j->ncomp >= 1) { j->comp[0].dc_htbl = 0; j->comp[0].ac_htbl = 0; }
    if (j->ncomp >= 2) { j->comp[1].dc_htbl = 1; j->comp[1].ac_htbl = 1; }
    if (j->ncomp >= 3) { j->comp[2].dc_htbl = 1; j->comp[2].ac_htbl = 1; }
    return 1;
}

static int parse_sos(JDec* j, const uint8_t* seg, int seg_len, int* sos_end) {
    if (seg_len < 1) return 0;
    int ns = seg[0];
    if (seg_len < 1 + ns * 2 + 3) return 0;
    for (int i = 0; i < ns; i++) {
        int cs = seg[1 + i*2];
        uint8_t td_ta = seg[2 + i*2];
        int td = (td_ta >> 4) & 1;
        int ta = (td_ta     ) & 1;
        /* Match component */
        for (int ci = 0; ci < j->ncomp; ci++) {
            if (j->comp[ci].id == cs) {
                j->comp[ci].dc_htbl = td;
                j->comp[ci].ac_htbl = ta;
                break;
            }
        }
    }
    *sos_end = 1 + ns*2 + 3;  /* skip Ss, Se, Ah/Al */
    return 1;
}

/* ─── YCbCr → RGB conversion (integer) ────────────────────────── */
/* Using fixed-point: multiply by 1024 and shift right 10 */
static void ycbcr_to_rgb(uint8_t y, uint8_t cb, uint8_t cr,
                          uint8_t* r, uint8_t* g, uint8_t* b) {
    int Y  = (int)y;
    int Cb = (int)cb - 128;
    int Cr = (int)cr - 128;
    /* R = Y + 1.402*Cr */
    *r = clip8(Y + (1435 * Cr >> 10));
    /* G = Y - 0.344136*Cb - 0.714136*Cr */
    *g = clip8(Y - (352 * Cb >> 10) - (731 * Cr >> 10));
    /* B = Y + 1.772*Cb */
    *b = clip8(Y + (1814 * Cb >> 10));
}

/* ─── Allocate component plane ─────────────────────────────────── */
static int alloc_plane(JDec* j, JComp* c) {
    int pw = j->mcus_x * c->h * 8;
    int ph = j->mcus_y * c->v * 8;
    c->plane_w = pw;
    c->plane_h = ph;
    c->plane = (uint8_t*)malloc(pw * ph);
    if (!c->plane) return 0;
    memset(c->plane, 128, pw * ph);
    return 1;
}

/* ─── Main decode ──────────────────────────────────────────────── */
int jpeg_decode(const uint8_t* data, int len,
                uint8_t** rgb_out, int* w_out, int* h_out) {
    if (!data || len < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return 0;

    JDec* j = (JDec*)malloc(sizeof(JDec));
    if (!j) return 0;
    memset(j, 0, sizeof(JDec));
    j->data = data;
    j->data_len = len;

    int ok = 0;
    int sos_found = 0;
    int pos = 2;  /* skip SOI */

    /* ── Parse markers ── */
    while (pos + 3 < len && !sos_found) {
        if (data[pos] != 0xFF) { pos++; continue; }
        while (pos < len && data[pos] == 0xFF) pos++;
        if (pos >= len) break;
        uint8_t marker = data[pos++];
        if (marker == 0xD9) break; /* EOI */
        if (marker == 0xD8) continue; /* SOI nested */
        /* Markers with no length: RST0-7, SOI, EOI */
        if ((marker >= 0xD0 && marker <= 0xD7)) continue;

        if (pos + 2 > len) break;
        int seg_len = (int)read_u16(data + pos) - 2;
        pos += 2;
        if (seg_len < 0 || pos + seg_len > len) break;
        const uint8_t* seg = data + pos;

        switch (marker) {
        case 0xC0: /* SOF0 — baseline DCT */
            if (!parse_sof0(j, seg, seg_len)) goto done;
            break;
        case 0xC4: /* DHT */
            if (!parse_dht(j, seg, seg_len)) goto done;
            break;
        case 0xDB: /* DQT */
            if (!parse_dqt(j, seg, seg_len)) goto done;
            break;
        case 0xDA: { /* SOS — scan header */
            int sos_skip = 0;
            if (!parse_sos(j, seg, seg_len, &sos_skip)) goto done;
            pos += seg_len;
            sos_found = 1;
            /* j->bits starts right after the SOS header */
            bits_init(&j->bits, data, len, pos);
            goto scan;
        }
        case 0xE0: case 0xE1: case 0xE2: /* APP0..APPn: skip */
        case 0xFE: /* COM: skip */
            break;
        default:
            break;
        }
        pos += seg_len;
    }
    if (!sos_found) goto done;

scan:
    /* Validate we have enough info */
    if (j->width <= 0 || j->height <= 0 || j->ncomp == 0) goto done;
    if (!j->valid_dc[0]) goto done;

    /* Allocate component planes */
    for (int ci = 0; ci < j->ncomp; ci++) {
        if (!alloc_plane(j, &j->comp[ci])) goto done;
    }

    /* Reset DC predictors */
    for (int ci = 0; ci < j->ncomp; ci++) j->comp[ci].dc_pred = 0;

    /* ── Decode MCUs ── */
    for (int mcu_y = 0; mcu_y < j->mcus_y; mcu_y++) {
        for (int mcu_x = 0; mcu_x < j->mcus_x; mcu_x++) {
            for (int ci = 0; ci < j->ncomp; ci++) {
                JComp* c = &j->comp[ci];
                int dc_id = c->dc_htbl & 1;
                int ac_id = c->ac_htbl & 1;
                if (!j->valid_ac[ac_id]) ac_id = 0;

                for (int vy = 0; vy < c->v; vy++) {
                    for (int vx = 0; vx < c->h; vx++) {
                        int blk[64];
                        memset(blk, 0, sizeof(blk));

                        /* DC */
                        int dc_sym = huff_decode(&j->bits, &j->dc_ht[dc_id]);
                        if (dc_sym < 0) goto store;
                        int dc_diff = 0;
                        if (dc_sym > 0) {
                            dc_diff = bits_get(&j->bits, dc_sym);
                            if (dc_diff < (1 << (dc_sym - 1)))
                                dc_diff -= (1 << dc_sym) - 1;
                        }
                        c->dc_pred += dc_diff;
                        blk[0] = c->dc_pred * j->quant[c->qtbl][0];

                        /* AC */
                        int k = 1;
                        while (k < 64) {
                            int ac_sym = huff_decode(&j->bits, &j->ac_ht[ac_id]);
                            if (ac_sym < 0) break;
                            if (ac_sym == 0x00) break;
                            if (ac_sym == 0xF0) { k += 16; continue; }
                            int run  = (ac_sym >> 4) & 0xF;
                            int size = (ac_sym     ) & 0xF;
                            k += run;
                            if (k >= 64) break;
                            int val = bits_get(&j->bits, size);
                            if (size > 0 && val < (1 << (size - 1)))
                                val -= (1 << size) - 1;
                            blk[ZIG[k]] = val * j->quant[c->qtbl][ZIG[k]];
                            k++;
                        }

                        /* iDCT into component plane */
                        int px = (mcu_x * c->h + vx) * 8;
                        int py = (mcu_y * c->v + vy) * 8;
                        if (px < c->plane_w && py < c->plane_h) {
                            uint8_t tmp[64];
                            idct_block(blk, tmp, 8);
                            for (int row = 0; row < 8; row++) {
                                int dst_y = py + row;
                                if (dst_y >= c->plane_h) break;
                                for (int col = 0; col < 8; col++) {
                                    int dst_x = px + col;
                                    if (dst_x >= c->plane_w) break;
                                    c->plane[dst_y * c->plane_w + dst_x] = tmp[row*8+col];
                                }
                            }
                        }
                    }
                }
            }
        }
    }

store:
    /* Assemble final RGB24 image */
    {
        int W = j->width, H = j->height;
        uint8_t* rgb = (uint8_t*)malloc(W * H * 3);
        if (!rgb) goto done;

        JComp* c0 = &j->comp[0];
        for (int py = 0; py < H; py++) {
            for (int px = 0; px < W; px++) {
                uint8_t y_val, cb_val, cr_val;
                /* Sample luma */
                int lx = px * c0->plane_w / W;
                int ly = py * c0->plane_h / H;
                if (lx >= c0->plane_w) lx = c0->plane_w - 1;
                if (ly >= c0->plane_h) ly = c0->plane_h - 1;
                y_val = c0->plane[ly * c0->plane_w + lx];

                if (j->ncomp == 1) {
                    rgb[(py*W+px)*3+0] = y_val;
                    rgb[(py*W+px)*3+1] = y_val;
                    rgb[(py*W+px)*3+2] = y_val;
                } else {
                    JComp* c1 = &j->comp[1];
                    JComp* c2 = &j->comp[2];
                    int cx = px * c1->plane_w / W;
                    int cy = py * c1->plane_h / H;
                    if (cx >= c1->plane_w) cx = c1->plane_w - 1;
                    if (cy >= c1->plane_h) cy = c1->plane_h - 1;
                    cb_val = c1->plane[cy * c1->plane_w + cx];
                    cx = px * c2->plane_w / W;
                    cy = py * c2->plane_h / H;
                    if (cx >= c2->plane_w) cx = c2->plane_w - 1;
                    if (cy >= c2->plane_h) cy = c2->plane_h - 1;
                    cr_val = c2->plane[cy * c2->plane_w + cx];

                    uint8_t r, g, b;
                    ycbcr_to_rgb(y_val, cb_val, cr_val, &r, &g, &b);
                    rgb[(py*W+px)*3+0] = r;
                    rgb[(py*W+px)*3+1] = g;
                    rgb[(py*W+px)*3+2] = b;
                }
            }
        }
        *rgb_out = rgb;
        *w_out   = W;
        *h_out   = H;
        ok = 1;
    }

done:
    for (int ci = 0; ci < j->ncomp; ci++) {
        if (j->comp[ci].plane) free(j->comp[ci].plane);
    }
    free(j);
    return ok;
}
