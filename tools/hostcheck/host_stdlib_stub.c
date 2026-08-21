#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
void* malloc(size_t s) {
    if (s == 0) return (void*)0x8;
    void* p = __builtin_malloc(s);
    return p;
}
void free(void* p) {
    if (!p || p == (void*)0x8) return;
    __builtin_free(p);
}
void* memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    while (n--) *p++ = (char)c;
    return s;
}
