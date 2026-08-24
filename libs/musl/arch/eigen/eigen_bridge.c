/* EigenOS musl bridge (part 2):
 *  - __syscall_cp: cancellation-point wrapper. EigenOS is single-threaded,
 *    so it just forwards to the raw syscall.
 *  - __errno_location: thread-independent errno (static slot).
 *  - _start: process bootstrap matching EigenOS's kernel entry ABI.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <features.h>
#include "syscall_arch.h"

#define EIGEN_SYS_ALLOC 7

/* NOTE: libpng/libjpeg reference the legacy `_eigen_stderr` identifier. Those
 * libraries are now compiled against musl with -D_eigen_stderr=stderr (see
 * build.sh build_png/build_jpeg), so the token maps to musl's real stderr and
 * no separate `_eigen_stderr` object is needed here. */

/* ---- __syscall_cp (declared hidden in src/internal/syscall.h) ---- */
hidden long __syscall_cp(syscall_arg_t n, syscall_arg_t a, syscall_arg_t b,
                         syscall_arg_t c, syscall_arg_t d, syscall_arg_t e,
                         syscall_arg_t f)
{
    return __syscall6(n, a, b, c, d, e, f);
}

/* ---- errno ---------------------------------------------------------- */
static int __eigen_errno;
hidden int *__errno_location(void)
{
    return &__eigen_errno;
}
weak_alias(__errno_location, ___errno_location);

/* ---- single-threaded locks (no-op) ---- */
hidden void __lock(volatile int *p) { (void)p; }
hidden void __unlock(volatile int *p) { (void)p; }

/* ---- public malloc wrapper (normally from replaced.c; realloc is already
 *      provided by src/malloc/realloc.o, so only malloc needs bridging) ---- */
void *__libc_malloc_impl(size_t);
void *malloc(size_t n) { return __libc_malloc_impl(n); }
void *__libc_malloc(size_t n) { return __libc_malloc_impl(n); }

/* ---- malloc interposition flags (normally in replaced.c) ---- */
int __malloc_replaced;
int __aligned_alloc_replaced;

/* ---- brk bridge (mallocng uses brk for its small-object heap) ----
 * Routed to a reserved musl "syscall" number (see bits/syscall.h SYS_brk). */
void *__eigen_brk(void *end);
void *__eigen_brk(void *end)
{
    static void *base;
    static void *cur;
    if (!base) {
        base = (void *)__syscall2(EIGEN_SYS_ALLOC, (long)(16u*1024*1024), 0);
        if (!base) return (void *)-1;
        cur = base;
    }
    if (end == 0) return cur;
    if ((uint8_t *)end < (uint8_t *)base) return (void *)-1;
    /* only allow advancing within the reserved 16 MB region */
    if ((uint8_t *)end > (uint8_t *)base + 16u*1024*1024) return (void *)-1;
    cur = end;
    return end;
}
