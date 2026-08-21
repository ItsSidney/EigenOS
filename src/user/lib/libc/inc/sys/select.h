/* select(2) shim for EigenOS ring-3 apps, built on poll(2). */
#ifndef EIGEN_SHIM_SYS_SELECT_H
#define EIGEN_SHIM_SYS_SELECT_H

#include <stdint.h>

#define FD_SETSIZE 1024
typedef struct {
    uint64_t bits[FD_SETSIZE / 64];
} fd_set;

#define FD_ZERO(set)  do { int _i; for (_i = 0; _i < FD_SETSIZE / 64; _i++) (set)->bits[_i] = 0; } while (0)
#define FD_SET(fd, set)   ((set)->bits[(fd) / 64] |= (1ULL << ((fd) % 64)))
#define FD_CLR(fd, set)   ((set)->bits[(fd) / 64] &= ~(1ULL << ((fd) % 64)))
#define FD_ISSET(fd, set) (((set)->bits[(fd) / 64] >> ((fd) % 64)) & 1)

#ifndef EIGEN_SHIM_POSIX_H
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif /* timeval lives in posix.h when both are included */

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
           struct timeval* timeout);

#endif