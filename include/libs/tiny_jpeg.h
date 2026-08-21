/* tiny_jpeg.h — Minimal freestanding baseline JPEG decoder.
 * Decodes SOF0 (baseline DCT) JPEG into packed RGB24.
 * Uses only kmalloc/kfree + memcpy/memset.
 * Integer-only arithmetic (no FPU).
 */
#ifndef TINY_JPEG_H
#define TINY_JPEG_H
#include <stdint.h>

/* Decode a JPEG image from raw bytes.
 * Returns 1 on success, 0 on failure.
 * On success, *rgb_out is a kmalloc'd buffer of w*h*3 bytes (packed RGB24).
 * The caller must kfree(*rgb_out) when done. */
int jpeg_decode(const uint8_t* data, int len,
                uint8_t** rgb_out, int* w_out, int* h_out);

#endif /* TINY_JPEG_H */
