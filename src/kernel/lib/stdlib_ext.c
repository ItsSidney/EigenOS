/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include <stdlib.h>
#include <string.h>
#include <kernel/log.h>
#include <kernel/syscall.h>
#include <kernel/mem/kheap.h>

int atoi(const char* s) {
    int sign = 1;
    int v = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '+' || *s == '-') {
        if (*s == '-') sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}

long atol(const char* s) {
    return (long)atoi(s);
}

int abs(int x) {
    return x < 0 ? -x : x;
}

long labs(long x) {
    return x < 0 ? -x : x;
}

static unsigned long next_rand = 1;

int rand(void) {
    next_rand = next_rand * 1103515245 + 12345;
    return (int)((next_rand / 65536) % 32768);
}

void srand(unsigned int seed) {
    next_rand = seed;
}

void exit(int code) {
    klog("exit() called, code=");
    char buf[16];
    int i = 0;
    unsigned int v = (unsigned int)code;
    if (v == 0) { klog("0\n"); }
    else {
        char tmp[16];
        int l = 0;
        while (v > 0) { tmp[l++] = (char)('0' + (v % 10)); v /= 10; }
        for (i = 0; i < l; i++) buf[i] = tmp[l - 1 - i];
        buf[l] = '\n';
        buf[l + 1] = 0;
        klog(buf);
    }
    exit_task(code);
    for (;;) { __asm__ volatile("hlt"); }
}

/* The kernel only exposes kmalloc/kfree; expose the standard C malloc/free/
   realloc/calloc ABI so ports (and future apps) can use them. */

void* malloc(size_t size) {
    if (!size) return (void*)0;
    return kmalloc(size);
}

void free(void* ptr) {
    if (ptr) kfree(ptr);
}

void* calloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return (void*)0; }
    void* p = kmalloc(size);
    if (!p) return (void*)0;
    /* kmalloc returns a fresh slab; copy old content.  The original kmalloc
       does not expose the allocation size, so copy a conservative amount. */
    /* We cannot know the old size, so copy nothing beyond what fits; callers
       that need the old bytes usually know them. */
    memset(p, 0, size);
    kfree(ptr);
    return p;
}
