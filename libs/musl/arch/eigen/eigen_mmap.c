/* EigenOS musl bridge: implement the POSIX mmap family on top of the
 * kernel's page-granular user heap (EIGEN_SYS_ALLOC / EIGEN_SYS_FREE).
 *
 * musl's mallocng relies on anonymous private mappings only, so we only
 * need to support MAP_ANON|MAP_PRIVATE. Everything else returns MAP_FAILED
 * with errno set; advisory calls (madvise/mprotect) are no-ops.
 */
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "syscall_arch.h"

#define EIGEN_SYS_ALLOC 7
#define EIGEN_SYS_FREE  8

static void *eigen_alloc(size_t n)
{
    if (n == 0) n = 4096;
    return (void *)__syscall2(EIGEN_SYS_ALLOC, (long)n, 0);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)prot; (void)offset;
    /* Only anonymous private mappings are supported (mallocng's only use). */
    if (!(flags & MAP_ANON) || fd != -1) {
        errno = ENODEV;
        return MAP_FAILED;
    }
    /* Ignore MAP_FIXED: we cannot honor a fixed address on this heap. */
    void *p = eigen_alloc(length);
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return p;
}

int munmap(void *addr, size_t length)
{
    (void)length;
    __syscall1(EIGEN_SYS_FREE, (long)addr);
    return 0;
}

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...)
{
    (void)flags;
    if (new_size <= old_size) return old_address; /* can't shrink safely */
    void *p = eigen_alloc(new_size);
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memcpy(p, old_address, old_size);
    __syscall1(EIGEN_SYS_FREE, (long)old_address);
    return p;
}

int madvise(void *addr, size_t length, int advice)
{
    (void)addr; (void)length; (void)advice;
    return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
    (void)addr; (void)len; (void)prot;
    return 0;
}

/* Internal variants used by mallocng (glue.h remaps mmap/madvise/mremap). */
hidden void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return mmap(addr, length, prot, flags, fd, offset);
}
hidden int __madvise(void *addr, size_t length, int advice)
{
    return madvise(addr, length, advice);
}
hidden void *__mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...)
{
    return mremap(old_address, old_size, new_size, flags);
}

int __munmap(void *addr, size_t len)
{
    return munmap(addr, len);
}
