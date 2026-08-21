/*
 * Memory allocator for TinyGL (Userland Ring-3)
 */
#include "zgl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void gl_free(void *p)
{
    if (p != NULL) { printf("[TGL] free %p\n", p); free(p); }
}

void *gl_malloc(int size)
{
    void *p = malloc(size);
    printf("[TGL] malloc %p size=%d\n", p, size);
    return p;
}

void *gl_zalloc(int size)
{
    void *p = malloc(size);
    printf("[TGL] zalloc %p size=%d\n", p, size);
    if (p != NULL) memset(p, 0, size);
    return p;
}
