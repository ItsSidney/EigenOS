#ifndef EIGEN_SHIM_SYS_SYSCALL_H
#define EIGEN_SHIM_SYS_SYSCALL_H
static inline long syscall(long n, ...) { (void)n; return -1; }
#endif
