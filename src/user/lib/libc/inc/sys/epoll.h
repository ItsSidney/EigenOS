/* epoll shim for EigenOS ring-3 apps (level-triggered only). */
#ifndef EIGEN_SHIM_SYS_EPOLL_H
#define EIGEN_SHIM_SYS_EPOLL_H

#include <stdint.h>

#define EPOLLIN      0x001
#define EPOLLPRI     0x002
#define EPOLLOUT     0x004
#define EPOLLERR     0x008
#define EPOLLHUP     0x010
#define EPOLLNVAL    0x020
#define EPOLLRDNORM  0x040
#define EPOLLWRNORM  0x100
#define EPOLLET      0x80000000u  /* accepted but not implemented (level-only) */

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_MOD 2
#define EPOLL_CTL_DEL 3

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

int epoll_create1(int flags);
int epoll_ctl(int epid, int op, int fd, struct epoll_event* event);
int epoll_wait(int epid, struct epoll_event* events, int maxevents, int timeout_ms);

#endif