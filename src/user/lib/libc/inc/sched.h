/* Minimal sched.h stub for the Eina freestanding port. */
#ifndef EIGEN_SHIM_SCHED_H
#define EIGEN_SHIM_SCHED_H

#include <sys/types.h>

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

struct sched_param {
    int sched_priority;
};

typedef unsigned long cpu_set_t;

#define CPU_SETSIZE 1024
#define CPU_ZERO(cpuset)      (*(cpuset) = 0UL)
#define CPU_SET(cpu, cpuset)  (*(cpuset) |= (1UL << (cpu)))
#define CPU_CLR(cpu, cpuset)  (*(cpuset) &= ~(1UL << (cpu)))
#define CPU_ISSET(cpu, cpuset) ((*(cpuset) & (1UL << (cpu))) != 0UL)

int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t* mask);
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t* mask);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
int sched_yield(void);
int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

#endif
