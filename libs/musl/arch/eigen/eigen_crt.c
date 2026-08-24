/* EigenOS musl process bootstrap (_start).
 *
 * The kernel jumps here with RSP pointing at the argument block:
 *     [RSP]      = argc
 *     [RSP+8 ..] = argv[0..argc-1], then a NULL terminator
 * (no envp/auxv are placed by the kernel). We align the stack, seed a
 * minimal environment, call the application main(), then exit().
 */
#include <stddef.h>
extern int main(int argc, char **argv);
extern void exit(int);

/* musl's internal environment pointer (set here; getenv/setenv use it). */
extern char **__environ;
static char *__eigen_env[] = { "HOME=/home/user", NULL };

__asm__(
    ".global _start\n"
    "_start:\n"
    "  mov (%rsp), %ebx\n"        /* argc (callee-saved) */
    "  lea 8(%rsp), %r12\n"       /* argv (callee-saved) */
    "  and $-16, %rsp\n"          /* align so `call main` enters at RSP%16==8 */
    "  mov %ebx, %edi\n"
    "  mov %r12, %rsi\n"
    "  call __eigen_crt_init\n"
    "  mov %ebx, %edi\n"           /* restore argc/argv for main */
    "  mov %r12, %rsi\n"           /* argv */
    "  call main\n"
    "  mov %eax, %edi\n"
    "  call exit\n"
    "_start_hang:\n"
    "  jmp _start_hang\n");

void __eigen_crt_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    __environ = __eigen_env;
}
