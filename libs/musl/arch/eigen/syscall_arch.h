/* EigenOS x86_64 syscall bridge for musl
 * Replaces musl's arch/x86_64/syscall_arch.h
 * Uses int 0x80 with EigenOS syscall table instead of `syscall` instruction. */

#ifndef _EIGEN_SYSCALL_ARCH_H
#define _EIGEN_SYSCALL_ARCH_H

#include <stdint.h>

/* hidden / weak_alias are provided by src/include/features.h (the internal
 * features.h on the include path), so we must NOT redefine them here. */

typedef long syscall_arg_t;

/* EigenOS is LP64 (64-bit long/off_t), so 64-bit args need no splitting. */
#define __SYSCALL_LL_O(x) (x)
#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_PRW(x) (x)

/* EigenOS int 0x80 convention:
 * rax = syscall number, rbx = arg1, rcx = arg2, rdx = arg3, rsi = arg4
 * Unsupported musl SYS_* numbers are mapped to 0x7fffffff (see
 * bits/syscall.h); short-circuit them here so they fail cleanly with
 * -ENOSYS instead of trapping into the kernel with a bogus number. */
#define __EIGEN_UNSUPPORTED(n) ((unsigned long)(n) >= 0x70000000UL)
#define __EIGEN_ENOSYS (-38L)
/* SYS_brk (0x7ff1) is implemented locally instead of trapping to the kernel. */
#define __EIGEN_BRK (0x7ff1L)
void *__eigen_brk(void *end);

static inline long __syscall0(long n) {
    long ret;
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n));
    return ret;
}

static inline long __syscall1(long n, long a) {
    long ret;
    if (n == __EIGEN_BRK) return (long)(intptr_t)__eigen_brk((void *)(intptr_t)a);
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    register long rbx __asm__("rbx") = a;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "r"(rbx) : "rcx","rdx","rsi");
    return ret;
}

static inline long __syscall2(long n, long a, long b) {
    long ret;
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    register long rbx __asm__("rbx") = a;
    register long rcx __asm__("rcx") = b;
    __asm__ volatile ("int $0x80" : "=a"(ret)
                      : "a"(n), "r"(rbx), "r"(rcx) : "rsi","rdx");
    return ret;
}

static inline long __syscall3(long n, long a, long b, long c) {
    long ret;
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    register long rbx __asm__("rbx") = a;
    register long rcx __asm__("rcx") = b;
    register long rdx __asm__("rdx") = c;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "r"(rbx), "r"(rcx), "r"(rdx) : "rsi");
    return ret;
}

static inline long __syscall4(long n, long a, long b, long c, long d) {
    long ret;
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    register long rbx __asm__("rbx") = a;
    register long rcx __asm__("rcx") = b;
    register long rdx __asm__("rdx") = c;
    register long rsi __asm__("rsi") = d;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "r"(rbx), "r"(rcx), "r"(rdx), "r"(rsi));
    return ret;
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e) {
    long ret;
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    register long rbx __asm__("rbx") = a;
    register long rcx __asm__("rcx") = b;
    register long rdx __asm__("rdx") = c;
    register long rsi __asm__("rsi") = d;
    register long rdi __asm__("rdi") = e;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "r"(rbx), "r"(rcx), "r"(rdx), "r"(rsi), "r"(rdi));
    return ret;
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    if (__EIGEN_UNSUPPORTED(n)) return __EIGEN_ENOSYS;
    register long rbx __asm__("rbx") = a;
    register long rcx __asm__("rcx") = b;
    register long rdx __asm__("rdx") = c;
    register long rsi __asm__("rsi") = d;
    register long rdi __asm__("rdi") = e;
    register long rbp __asm__("rbp") = f;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "r"(rbx), "r"(rcx), "r"(rdx), "r"(rsi), "r"(rdi), "r"(rbp));
    return ret;
}

#define SYSCALL_RVAL(x) (x)

#endif /* _EIGEN_SYSCALL_ARCH_H */
