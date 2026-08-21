/* setjmp/longjmp for ring-3 apps (x86-64 SysV).
 * Saves the six callee-saved registers plus rsp/rip; that is everything
 * the awk interpreter's error recovery needs. */
#include <setjmp.h>

/* layout: [0]=rbx [1]=rbp [2]=r12 [3]=r13 [4]=r14 [5]=r15
           [6]=rsp [7]=rip */
__attribute__((naked)) int setjmp(jmp_buf env) {
    __asm__(
        "movq %rbx,  0(%rdi)\n\t"
        "movq %rbp,  8(%rdi)\n\t"
        "movq %r12, 16(%rdi)\n\t"
        "movq %r13, 24(%rdi)\n\t"
        "movq %r14, 32(%rdi)\n\t"
        "movq %r15, 40(%rdi)\n\t"
        "leaq 8(%rsp), %rax\n\t"
        "movq %rax, 48(%rdi)\n\t"
        "movq (%rsp), %rax\n\t"
        "movq %rax, 56(%rdi)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val) {
    __asm__(
        "movl %esi, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n\t"
        "1:\n\t"
        "movq 0(%rdi), %rbx\n\t"
        "movq 8(%rdi), %rbp\n\t"
        "movq 16(%rdi), %r12\n\t"
        "movq 24(%rdi), %r13\n\t"
        "movq 32(%rdi), %r14\n\t"
        "movq 40(%rdi), %r15\n\t"
        "movq 48(%rdi), %rsp\n\t"
        "jmp *56(%rdi)\n\t");
}