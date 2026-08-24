/* zlib allocator for the kernel-side PNG decoder (Nordzy icon theme).
 * Kernel zlib builds with -DZ_SOLO -DMY_ZCALLOC so zutil.c routes all
 * allocations here instead of expecting a libc calloc/free. */

#include <stddef.h>
#include <string.h>
#include "zutil.h"
#include "kernel/mem/kheap.h"

voidpf ZLIB_INTERNAL zcalloc(voidpf opaque, unsigned items, unsigned size) {
    (void)opaque;
    size_t total = (size_t)items * (size_t)size;
    if (total == 0) total = 1;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return (voidpf)p;
}

void ZLIB_INTERNAL zcfree(voidpf opaque, voidpf ptr) {
    (void)opaque;
    if (ptr) kfree(ptr);
}
