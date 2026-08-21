/* zlibtest.c — minimal zlib (Z_SOLO) smoke test for EigenOS.
 * Compresses a buffer, decompresses it, and verifies the round-trip.
 * Output goes via eigen_puts (serial) so it's visible even on crash. */

#include <zlib.h>
#include <userlib.h>
#include <string.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    eigen_puts("[ZLIBTEST] START\n");

    /* Build a compressible payload (repeating pattern). */
    char src[4096];
    for (int i = 0; i < (int)sizeof(src); i++)
        src[i] = (char)('A' + (i % 10));
    uLong srcLen = sizeof(src);

    Bytef compr[8192];
    uLongf comprLen = sizeof(compr);
    int r = compress(compr, &comprLen, (const Bytef *)src, srcLen);
    if (r != Z_OK) {
        eigen_puts("[ZLIBTEST] compress FAILED\n");
        return 1;
    }
    eigen_puts("[ZLIBTEST] compress OK\n");

    Bytef uncompr[4096];
    uLongf uncomprLen = sizeof(uncompr);
    r = uncompress(uncompr, &uncomprLen, compr, comprLen);
    if (r != Z_OK) {
        eigen_puts("[ZLIBTEST] uncompress FAILED\n");
        return 1;
    }
    eigen_puts("[ZLIBTEST] uncompress OK\n");

    if (uncomprLen != srcLen || memcmp(src, uncompr, srcLen) != 0) {
        eigen_puts("[ZLIBTEST] round-trip MISMATCH\n");
        return 1;
    }
    eigen_puts("[ZLIBTEST] round-trip OK\n");
    eigen_puts("[ZLIBTEST] ALL PASS\n");
    return 0;
}
