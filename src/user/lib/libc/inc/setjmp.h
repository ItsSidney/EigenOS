/* Minimal setjmp.h for ring-3 apps: x86-64 SysV non-volatile register set.
 * Implemented in setjmp.c with plain registers (no FPU state needed). */
#ifndef EIGEN_SHIM_SETJMP_H
#define EIGEN_SHIM_SETJMP_H

typedef struct { unsigned long regs[8]; } jmp_buf[1];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif