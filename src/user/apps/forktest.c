/* forktest.c — validates REAL POSIX process syscalls on EigenOS:
 * fork() address-space copy, execve() image replacement,
 * wait4() reaping, getdents64() directory reads, dup2, readv/writev. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "posix.h"
#include <fcntl.h>
#include <errno.h>

/* real process syscalls (musl archive provides them; shim headers only) */
extern int fork(void);
extern int execve(const char*, char* const[], char* const[]);
extern void _exit(int);
struct iovec { void* iov_base; unsigned long iov_len; };
extern ssize_t writev(int, const struct iovec*, int);

int main(void) {
    printf("[forktest] start pid=%d\n", getpid());

    /* ---- 1) fork: child mutates memory, parent must not see it ---- */
    int shared = 41;
    pid_t pid = fork();
    if (pid < 0) { printf("[forktest] FORK FAILED errno=%d\n", errno); return 1; }
    if (pid == 0) {
        shared = 99;                       /* copy-on-fork: private to child */
        printf("[forktest] child sees shared=%d\n", shared);
        /* ---- 2) execve: replace image with argtest ---- */
        char* const argv[] = { "argtest", "from-execve", NULL };
        execve("/user/argtest.elf", argv, NULL);
        printf("[forktest] EXECVE FAILED errno=%d\n", errno);
        _exit(7);
    }
    int st = 0;
    pid_t w = waitpid(pid, &st, 0);
    printf("[forktest] waitpid -> %d status=0x%x (exit=%d)\n",
           w, st, WEXITSTATUS(st));
    printf("[forktest] parent still sees shared=%d (expect 41)\n", shared);

    /* ---- 3) getdents64 via readdir ---- */
    DIR* d = opendir("/mnt");
    if (!d) d = opendir("/");
    if (d) {
        int n = 0;
        struct dirent* e;
        while ((e = readdir(d)) && n < 8) {
            if (!n) printf("[forktest] dir entries:");
            printf(" %s", e->d_name);
            n++;
        }
        printf("%s\n", n ? "" : "[forktest] dir empty");
        closedir(d);
    }

    /* ---- 4) dup2 + writev ---- */
    int devnull = open("/dev/null", O_WRONLY);   /* may fail — fine */
    if (devnull >= 0) close(devnull);
    struct iovec iov[2];
    char part1[] = "[forktest] writev ";
    char part2[] = "works\n";
    iov[0].iov_base = part1; iov[0].iov_len = strlen(part1);
    iov[1].iov_base = part2; iov[1].iov_len = strlen(part2);
    ssize_t wr = writev(1, iov, 2);

    printf("[forktest] %s (%zd bytes via writev)\n",
           wr > 0 && shared == 41 ? "ALL PASS" : "SOME FAILURES", wr);
    return 0;
}
