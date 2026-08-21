/*
 * Memory allocator for TinyGL
 */
#include "zgl.h"
#include "kernel/mem/kheap.h"
#include "string.h"

void gl_free(void *p)
{
    if (p != NULL) kfree(p);
}

void *gl_malloc(int size)
{
    return kmalloc(size);
}

void *gl_zalloc(int size)
{
    void *p = kmalloc(size);
    if (p != NULL) memset(p, 0, size);
    return p;
}
