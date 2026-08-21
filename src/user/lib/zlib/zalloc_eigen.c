/* zalloc_eigen.c -- zlib memory allocator backed by EigenOS eigen_malloc.
 *
 * Z_SOLO builds of zlib omit the default malloc-based zcalloc/zcfree, and the
 * convenience callers (compress/uncompress) must therefore supply an allocator.
 * This file provides one mapping to the EigenOS user heap (eigen_malloc/free). */

#include "zutil.h"
#include "userlib.h"

voidpf ZLIB_INTERNAL zcalloc(voidpf opaque, unsigned items, unsigned size) {
    (void)opaque;
    return (voidpf)eigen_malloc((size_t)items * (size_t)size);
}

void ZLIB_INTERNAL zcfree(voidpf opaque, voidpf ptr) {
    (void)opaque;
    if (ptr) eigen_free(ptr);
}
