/* Minimal sys/resource.h stub for the Eina port. */
#ifndef EIGEN_SHIM_SYSRESOURCE_H
#define EIGEN_SHIM_SYSRESOURCE_H

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_NOFILE   7
#define RLIMIT_AS       9

#define RLIM_INFINITY (~0UL)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY

int getrlimit(int resource, struct rlimit* rlim);
int setrlimit(int resource, const struct rlimit* rlim);

#endif
