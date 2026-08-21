/* Freestanding sys/time.h shim for EigenOS ring-3 / EFL port. */
#ifndef EIGEN_SHIM_SYS_TIME_H
#define EIGEN_SHIM_SYS_TIME_H
#include <stddef.h>
#include <sys/types.h>

typedef long time_t;
typedef long suseconds_t;

struct timeval {
    time_t      tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif /* EIGEN_SHIM_SYS_TIME_H */
