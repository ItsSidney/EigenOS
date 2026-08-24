/* Freestanding errno.h for ring-3 user apps.
   errno is per-thread: pthread.c provides __errno_location() which reads
   the thread's TLS page (IA32_FS_BASE). Values are POSIX-compatible
   numbers so ported code that tests `errno == ENOENT` etc. works
   unchanged. */
#ifndef EIGEN_SHIM_ERRNO_H
#define EIGEN_SHIM_ERRNO_H

#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define ENXIO   6
#define EBADF   9
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define ECHILD  10
#define EEXIST  17
#define ENODEV  19
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define EFBIG   27
#define ENOSPC  28
#define ESPIPE  29
#define EROFS   30
#define EMLINK  31
#define EDOM    33
#define ERANGE  34
#define EDEADLK 35
#define ENOSYS  38
#define ENOTEMPTY 39
#define ENAMETOOLONG 36
#define ECANCELED 125
#define ENOTSUP  95

#define EWOULDBLOCK EAGAIN
#define EBUSY     16
#define ETIMEDOUT 110

/* Per-thread errno (TLS slot 0, see pthread.c). */
int* __errno_location(void);
#define errno (*__errno_location())

#define ELOOP 40
#endif
#define EOVERFLOW 75
