/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef BMP_H
#define BMP_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int bpp;
    uint8_t* pixels;
    uint32_t palette[256];
    int palette_size;
} bmp_image_t;

int bmp_decode(const uint8_t* data, int len, bmp_image_t* img);
void bmp_free(bmp_image_t* img);
int bmp_encode(const bmp_image_t* img, uint8_t** out_data, int* out_len);

#endif
