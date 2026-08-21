/* Freestanding sys/mman.h shim for EigenOS ring-3 / EFL port.
 * EigenOS does not expose mmap to ring-3; Eina only calls mmap in
 * eina_file_posix.c which we exclude from the build. */
#ifndef EIGEN_SHIM_MMAN_H
#define EIGEN_SHIM_MMAN_H
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED  ((void*)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
int   munmap(void *addr, size_t length);

#endif /* EIGEN_SHIM_MMAN_H */
