/* tiny_png.h — Minimal freestanding PNG decoder.
 * Decodes PNG (all standard color types) into packed RGB24.
 * Uses only kmalloc/kfree + memcpy/memset.
 */
#ifndef TINY_PNG_H
#define TINY_PNG_H
#include <stdint.h>

/* Decode a PNG image from raw bytes.
 * Returns 1 on success, 0 on failure.
 * On success, *rgb_out is a kmalloc'd buffer of w*h*3 bytes (packed RGB24).
 * The caller must kfree(*rgb_out) when done. */
int png_decode(const uint8_t* data, int len,
               uint8_t** rgb_out, int* w_out, int* h_out);

#endif /* TINY_PNG_H */
