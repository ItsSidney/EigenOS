/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/
/*
 * FreeType system interface for the EigenOS kernel (ring 0).
 * Replaces freetype's stock base/ftsystem.c:
 *   - memory manager backed by the kernel heap (kmalloc/kfree)
 *   - FT_New_Memory / FT_Done_Memory return that manager
 *   - file streams are not supported: FT_Stream_Open always fails,
 *     which is fine because the shell only ever uses
 *     FT_New_Memory_Face() on the DejaVuSans boot module.
 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H
#include "kernel/mem/kheap.h"
#include <string.h>

static void* ft_kalloc(FT_Memory memory, long size) {
    (void)memory;
    if (size <= 0) return 0;
    return kmalloc((size_t)size);
}

static void ft_kfree(FT_Memory memory, void* block) {
    (void)memory;
    if (block) kfree(block);
}

static void* ft_krealloc(FT_Memory memory, long cur_size, long new_size, void* block) {
    (void)memory; (void)cur_size;
    if (new_size <= 0) { if (block) kfree(block); return 0; }
    if (!block) return kmalloc((size_t)new_size);
    void* np = kmalloc((size_t)new_size);
    if (!np) return 0;
    long copy = cur_size < new_size ? cur_size : new_size;
    if (copy > 0) memcpy(np, block, (size_t)copy);
    kfree(block);
    return np;
}

static struct FT_MemoryRec_ g_ft_memory = {
    0,
    ft_kalloc,
    ft_kfree,
    ft_krealloc
};

FT_Memory FT_New_Memory(void) {
    return &g_ft_memory;
}

void FT_Done_Memory(FT_Memory memory) {
    (void)memory; /* static record — nothing to release */
}

FT_Error FT_Stream_Open(FT_Stream stream, const char* filepathname) {
    (void)filepathname;
    stream->base = 0;
    stream->size = 0;
    stream->close = 0;
    stream->read = 0;
    return -1; /* no file streams in the kernel — use FT_New_Memory_Face */
}

/* ── tiny libc pieces FreeType's ftstdlib.h expects ─────────── */

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    while (n--) {
        if (*p == (unsigned char)c) return (void*)p;
        p++;
    }
    return 0;
}

typedef int (*ft_qsort_cmp)(const void*, const void*);

static void ft_qsort_swap(char* a, char* b, size_t size) {
    while (size--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void* base, size_t nmemb, size_t size, ft_qsort_cmp cmp) {
    /* insertion sort — fine for the small arrays FreeType sorts */
    char* b = (char*)base;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0; j--) {
            if (cmp(b + (j - 1) * size, b + j * size) <= 0) break;
            ft_qsort_swap(b + (j - 1) * size, b + j * size, size);
        }
    }
}

long strtol(const char* s, char** endp, int base) {
    long v = 0;
    int neg = 0, any = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') base = 8;
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        any = 1;
        s++;
    }
    if (endp) *(const char**)endp = any ? s : (const char*)0;
    return neg ? -v : v;
}

char* getenv(const char* name) {
    (void)name;
    return 0; /* no environment in the kernel */
}

/* ── setjmp/longjmp (integer regs, see include/setjmp.h) ────── */
/* layout: [0]=rbx [1]=rbp [2]=r12 [3]=r13 [4]=r14 [5]=r15
           [6]=rsp [7]=rip */

__attribute__((naked)) int setjmp(jmp_buf env) {
    __asm__(
        "movq %rbx,  0(%rdi)\n\t"
        "movq %rbp,  8(%rdi)\n\t"
        "movq %r12, 16(%rdi)\n\t"
        "movq %r13, 24(%rdi)\n\t"
        "movq %r14, 32(%rdi)\n\t"
        "movq %r15, 40(%rdi)\n\t"
        "leaq 8(%rsp), %rax\n\t"
        "movq %rax, 48(%rdi)\n\t"
        "movq (%rsp), %rax\n\t"
        "movq %rax, 56(%rdi)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val) {
    __asm__(
        "movl %esi, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n\t"
        "1:\n\t"
        "movq 0(%rdi), %rbx\n\t"
        "movq 8(%rdi), %rbp\n\t"
        "movq 16(%rdi), %r12\n\t"
        "movq 24(%rdi), %r13\n\t"
        "movq 32(%rdi), %r14\n\t"
        "movq 40(%rdi), %r15\n\t"
        "movq 48(%rdi), %rsp\n\t"
        "jmp *56(%rdi)\n\t");
}
