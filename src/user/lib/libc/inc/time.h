/* Minimal time.h: epoch seconds from the RTC via the kernel. */
#ifndef EIGEN_SHIM_TIME_H
#define EIGEN_SHIM_TIME_H

#include <sys/types.h>   /* for time_t/size_t where provided */
#include <stddef.h>       /* size_t (GCC built-in header) */

#ifndef _TIME_T_DEFINED
typedef long time_t;
#define _TIME_T_DEFINED
#endif

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_PROF 3

#ifndef _TM_DEFINED
#define _TM_DEFINED
struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};
#endif

time_t time(time_t* out);
int clock_gettime(int clk_id, struct timespec* ts);
int gettimeofday(struct timeval* tv, void* tz);

struct tm* localtime(const time_t* t);
struct tm* gmtime(const time_t* t);
time_t mktime(struct tm* tm);
char* asctime(const struct tm* tm);
char* ctime(const time_t* t);
size_t strftime(char* s, size_t max, const char* fmt, const struct tm* tm);
char* strptime(const char* s, const char* fmt, struct tm* tm);

#endif
