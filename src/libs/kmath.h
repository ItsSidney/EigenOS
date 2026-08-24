/* Kernel-side shims for the kernel libpng/zlib pair (icon decoder).
 * Force-included via `-include src/libs/kmath.h` in build_pngk. */
#ifndef KMATH_SHIM_H
#define KMATH_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Frexp: split x into mantissa in [0.5,1) and exponent 2^e. */
__attribute__((unused)) static double frexp(double x, int* ep) {
    union { double d; uint64_t u; } v;
    v.d = x;
    uint32_t se = (uint32_t)(v.u >> 52) & 0x7FFu;
    if (x == 0.0 || se == 0x7FFu) { if (ep) *ep = 0; return x; }
    int e = (int)se - 1022;
    if (ep) *ep = e;
    v.u = (v.u & ~(0x7FFULL << 52)) | ((uint64_t)1022 << 52);
    return v.d;
}

/* modf: split into integer part (toward zero) and fractional remainder. */
__attribute__((unused)) static double modf(double x, double* ip) {
    long long t = (long long)x;
    double d = (double)t;
    if (ip) *ip = d;
    return x - d;
}

/* ── libc surface libpng touches but the kernel lacks ─────────────── */

__attribute__((weak)) int errno;

__attribute__((noreturn)) __attribute__((weak)) void abort(void) {
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

__attribute__((weak)) char* strerror(int e) { (void)e; return "kernel png error"; }
__attribute__((weak)) int*  __errno_location(void) { return &errno; }

/* stdio: every path that could call these is dead in the icon decoder. */
__attribute__((weak)) FILE* fopen(const char* a, const char* b) { (void)a; (void)b; return 0; }
__attribute__((weak)) int    fclose(FILE* f)                     { (void)f; return -1; }
__attribute__((weak)) size_t fread(void* p, size_t s, size_t n, FILE* f) {
    (void)p; (void)s; (void)n; (void)f; return 0;
}
__attribute__((weak)) int remove(const char* p) { (void)p; return -1; }
__attribute__((weak)) int rename(const char* a, const char* b) { (void)a; (void)b; return -1; }
__attribute__((weak)) int ferror(FILE* f) { (void)f; return 0; }
__attribute__((weak)) int fflush(FILE* f) { (void)f; return 0; }
__attribute__((weak)) size_t fwrite(const void* p, size_t s, size_t n, FILE* f) {
    (void)p; (void)s; (void)n; (void)f; return 0;
}

#endif /* KMATH_SHIM_H */
