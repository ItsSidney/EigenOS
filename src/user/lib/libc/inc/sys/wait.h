/* Minimal sys/wait.h: system() is a stub here, so waitpid never blocks. */
#ifndef EIGEN_SHIM_SYS_WAIT_H
#define EIGEN_SHIM_SYS_WAIT_H
#include <sys/types.h>

#define WNOHANG   1
#define WUNTRACED 2

#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) ((((s) & 0x7f) + 1) >> 1 > 0)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)    (((s) >> 8) & 0xff)

pid_t wait(int* status);
pid_t waitpid(pid_t pid, int* status, int options);

#endif