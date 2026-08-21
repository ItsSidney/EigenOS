/* Minimal freestanding fcntl.h for the DOOM port (no real FS). */
#ifndef EIGEN_SHIM_FCNTL_H
#define EIGEN_SHIM_FCNTL_H
#include <sys/types.h>
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0x40
#define O_TRUNC    0x200
#define O_APPEND   0x400
#define O_NONBLOCK 0x800
#define O_BINARY   0
/* open() is provided by the libc shim. */
int open(const char* path, int flags, ...);
int fcntl(int fd, int cmd, ...);
#define F_GETFL 3
#define F_SETFL 4
#define F_SETFD 2
#define FD_CLOEXEC 1
#endif
