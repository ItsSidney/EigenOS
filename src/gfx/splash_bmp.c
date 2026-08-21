/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include <gfx/splash_bmp.h>
#include "drivers/video/framebuffer.h"
#include <stdint.h>

/* Defines are in splash_bmp.h */
#include <gfx/banner_array.inc>
#include <gfx/logo_array.inc>

static inline uint32_t rgb_to_pixel_local(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

int draw_bmp(int x, int y, const unsigned char* data, unsigned int len,
             uint32_t* dst_base, int dst_stride, int dst_w, int dst_h,
             unsigned int src_w, unsigned int src_h,
             int draw_w, int draw_h) {
    if (!data || len < 54) return -1;
    if (data[0] != 'B' || data[1] != 'M') return -1;

    unsigned int bmp_w = *(unsigned int*)(data + 18);
    int bmp_h = *(int*)(data + 22);
    unsigned int bpp = *(unsigned short*)(data + 28);
    unsigned int offset = *(unsigned int*)(data + 10);
    if (bpp != 24) return -1;
    if (bmp_w > 4096 || (bmp_h > 4096 && bmp_h < -4096)) return -1;

    /* Source crop region (default = full image) */
    unsigned int crop_x = 0, crop_y = 0, crop_w = bmp_w;
    unsigned int crop_h_abs = bmp_h < 0 ? -bmp_h : bmp_h;
    unsigned int crop_h = crop_h_abs;
    if (src_w > 0 && src_h > 0) {
        crop_w = src_w;
        crop_h = src_h;
        if (crop_x + crop_w > bmp_w) crop_w = bmp_w - crop_x;
        if (crop_y + crop_h > crop_h_abs) crop_h = crop_h_abs - crop_y;
    }

    /* Destination draw size (default = source size, no scaling) */
    int out_w = draw_w > 0 ? draw_w : (int)crop_w;
    int out_h = draw_h > 0 ? draw_h : (int)crop_h;

    int src_stride = ((bmp_w * 3 + 3) / 4) * 4;
    const unsigned char* base_src = data + offset;

    for (int row = 0; row < out_h; row++) {
        int dst_y = y + row;
        if (dst_y < 0 || dst_y >= dst_h) continue;

        /* Map destination row back to source row (nearest-neighbor) */
        unsigned int src_row = (unsigned int)((row * crop_h) / out_h);
        src_row = crop_y + src_row;
        if (src_row >= crop_h_abs) src_row = crop_h_abs - 1;

        /* BMP bottom-up: source row 0 is bottom, so flip */
        int actual_src_row;
        if (bmp_h > 0) {
            actual_src_row = (int)crop_h_abs - 1 - (int)src_row;
        } else {
            actual_src_row = (int)src_row;
        }
        if (actual_src_row < 0 || actual_src_row >= (int)crop_h_abs) continue;

        const unsigned char* s = base_src + actual_src_row * src_stride + crop_x * 3;
        for (int col = 0; col < out_w; col++) {
            int dst_x = x + col;
            if (dst_x >= 0 && dst_x < dst_w) {
                unsigned int src_col = (unsigned int)((col * crop_w) / out_w);
                if (src_col >= crop_w) src_col = crop_w - 1;
                const unsigned char* p = s + src_col * 3;
                uint8_t b = p[0], g = p[1], r = p[2];
                dst_base[dst_y * dst_stride + dst_x] = (r << 16) | (g << 8) | b;
            }
        }
    }
    return 0;
}

int draw_bmp_black_transparent(int x, int y, const unsigned char* data, unsigned int len,
                               uint32_t* dst_base, int dst_stride, int dst_w, int dst_h,
                               unsigned int src_w, unsigned int src_h,
                               int draw_w, int draw_h) {
    if (!data || len < 54) return -1;
    if (data[0] != 'B' || data[1] != 'M') return -1;

    unsigned int bmp_w = *(unsigned int*)(data + 18);
    int bmp_h = *(int*)(data + 22);
    unsigned int bpp = *(unsigned short*)(data + 28);
    unsigned int offset = *(unsigned int*)(data + 10);
    if (bpp != 24) return -1;
    if (bmp_w > 4096 || (bmp_h > 4096 && bmp_h < -4096)) return -1;

    unsigned int crop_x = 0, crop_y = 0, crop_w = bmp_w;
    unsigned int crop_h_abs = bmp_h < 0 ? -bmp_h : bmp_h;
    unsigned int crop_h = crop_h_abs;
    if (src_w > 0 && src_h > 0) {
        crop_w = src_w;
        crop_h = src_h;
        if (crop_x + crop_w > bmp_w) crop_w = bmp_w - crop_x;
        if (crop_y + crop_h > crop_h_abs) crop_h = crop_h_abs - crop_y;
    }

    int out_w = draw_w > 0 ? draw_w : (int)crop_w;
    int out_h = draw_h > 0 ? draw_h : (int)crop_h;

    int src_stride = ((bmp_w * 3 + 3) / 4) * 4;
    const unsigned char* base_src = data + offset;

    for (int row = 0; row < out_h; row++) {
        int dst_y = y + row;
        if (dst_y < 0 || dst_y >= dst_h) continue;

        unsigned int src_row = (unsigned int)((row * crop_h) / out_h);
        src_row = crop_y + src_row;
        if (src_row >= crop_h_abs) src_row = crop_h_abs - 1;

        int actual_src_row;
        if (bmp_h > 0) {
            actual_src_row = (int)crop_h_abs - 1 - (int)src_row;
        } else {
            actual_src_row = (int)src_row;
        }
        if (actual_src_row < 0 || actual_src_row >= (int)crop_h_abs) continue;

        const unsigned char* s = base_src + actual_src_row * src_stride + crop_x * 3;
        for (int col = 0; col < out_w; col++) {
            int dst_x = x + col;
            if (dst_x >= 0 && dst_x < dst_w) {
                unsigned int src_col = (unsigned int)((col * crop_w) / out_w);
                if (src_col >= crop_w) src_col = crop_w - 1;
                const unsigned char* p = s + src_col * 3;
                uint8_t b = p[0], g = p[1], r = p[2];
                if (r == 0 && g == 0 && b == 0) continue; /* skip black */
                dst_base[dst_y * dst_stride + dst_x] = (r << 16) | (g << 8) | b;
            }
        }
    }
    return 0;
}
