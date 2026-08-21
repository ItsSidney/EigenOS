/* poll(2) shim for EigenOS ring-3 apps. Backed by EIGEN_SYS_POLL. */
#ifndef EIGEN_SHIM_POLL_H
#define EIGEN_SHIM_POLL_H

#include <stddef.h>

typedef unsigned long nfds_t;

struct pollfd {
    int   fd;         /* -1 to ignore */
    short events;     /* requested */
    short revents;    /* returned */
};

#define POLLIN      0x001
#define POLLPRI     0x002
#define POLLOUT     0x004
#define POLLERR     0x008
#define POLLHUP     0x010
#define POLLNVAL    0x020
#define POLLRDNORM  0x040
#define POLLRDBAND  0x080
#define POLLWRNORM  0x100
#define POLLWRBAND  0x200
#define POLLRDHUP   0x2000

int poll(struct pollfd* fds, nfds_t nfds, int timeout_ms);

#endif