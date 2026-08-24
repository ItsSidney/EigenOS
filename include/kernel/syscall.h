/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* Syscall numbers — the API81 ABI. NEVER renumber; append only.
   Mirror of include/user/eigen.h (kept in sync). */
#define SYS_OPEN          0
#define SYS_WRITE         1
#define SYS_READ          2
#define SYS_CLOSE         3
#define SYS_GETPID        4
#define SYS_SLEEP         5
#define SYS_GETTIME       6
#define SYS_ALLOC         7
#define SYS_FREE          8
#define SYS_EXIT          9
#define SYS_SPAWN        10
#define SYS_WAIT         11
#define SYS_SYSINFO      12
#define SYS_GFX          13
#define SYS_INPUT        14
#define SYS_BEEP         15
#define SYS_WIN          16
#define SYS_TIME         17
#define SYS_FONTMAP      18
#define SYS_MODLOAD      19   /* copy a boot module's bytes into a user buffer */
#define SYS_FS           20   /* filesystem: mkdir/create/delete/rename/... */
#define SYS_GET_THEME    21
#define SYS_LSEEK        22
#define SYS_SETTINGS     23
#define SYS_NET          24
#define SYS_THREAD       25   /* threads: create/exit/join/yield (same process) */
#define SYS_FUTEX        26   /* futex: wait/wake for userland sync primitives */
#define SYS_POLL         27   /* poll(2): (pollfd*, nfds, timeout_ms) -> ready count */
#define SYS_EPOLL        28   /* epoll: (sub-op, ...) level-triggered monitors */
#define SYS_PIPE         29   /* pipe(2): (int[2] pipefd) -> 0 */
#define SYS_FCNTL        30   /* fcntl(2): (fd, cmd, arg) -> val */
#define SYS_SIGNAL       32   /* signal(sig, handler) - install handler          */
#define SYS_KILL         33   /* kill(pid, sig)                                  */
#define SYS_SIGRETURN    34   /* internal: trampoline return from handler        */
#define SYSCALL_COUNT    45

/* SYS_EPOLL sub-operations (mirror of include/user/eigen.h) */
#define EPOLL_EPOLL_CREATE  0
#define EPOLL_EPOLL_CTL     1
#define EPOLL_EPOLL_WAIT    2

/* fcntl(2) command numbers (subset: F_GETFL / F_SETFL, plus EBADF/EMFILE errno) */
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 0x800

/* POSIX errno values for negative syscall returns (mirror libc errno.h:
   userland wrappers set errno = -ret). */
#define ENOENT   2
#define EBADF    9
#define EEXIST  17
#define EINVAL  22
#define EMFILE  23
#define ENFILE  24
#define ENOSPC  28

/* poll(2) event/revents bits — Linux values so ported code works. */
#define POLLIN      0x001
#define POLLPRI     0x002
#define POLLOUT     0x004
#define POLLERR     0x008
#define POLLHUP     0x010
#define POLLNVAL    0x020
#define POLLRDNORM  0x040
#define POLLWRNORM  0x100

/* ABI structs (must match libc poll.h / sys/epoll.h). */
struct eigen_pollfd {
    int   fd;
    short events;
    short revents;
};
struct eigen_epoll_ctl {        /* epoll_ctl(2) args packed for the syscall */
    int      fd;
    uint32_t events;
    uint64_t data;
};
struct eigen_epoll_event {      /* epoll_wait(2) output entry */
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

// Forward declarations
struct vfs_node;
struct task_t;

int sys_open(const char* path, int flags);
int sys_write(int fd, const char* buf, uint32_t count);
int sys_read(int fd, char* buf, uint32_t count);
void sys_close(int fd);
int sys_pipe(int pipefd[2]);             /* -> 0 ok, <0 err */
int sys_fcntl(int fd, int cmd, int arg); /* -> value, <0 err */
int get_current_task_id(void);
void sleep_task(uint32_t ms);
uint32_t timer_get_ms(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void exit_task(int code);

// Syscall handler (called from asm stub): num + up to 4 args
uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

#endif