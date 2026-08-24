/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/
/*
 * Minimal kernel setjmp.h (x86-64 SysV, integer registers only).
 * Needed by the kernel-side FreeType port (error unwinding inside
 * the rasterizer); implementation lives in lib/ftsystem_kernel.c.
 */

#ifndef _SETJMP_H
#define _SETJMP_H

typedef long long jmp_buf[8]; /* rbx rbp r12 r13 r14 r15 rsp rip */

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
