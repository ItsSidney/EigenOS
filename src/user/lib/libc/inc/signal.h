/* Minimal signal.h: ring-3 tasks get no signal delivery; provide the
 * API surface awk needs so handlers compile but never fire. */
#ifndef EIGEN_SHIM_SIGNAL_H
#define EIGEN_SHIM_SIGNAL_H

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV  11
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

typedef unsigned long sigset_t;

#define SIGCHLD  17
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGHUP   1
#define SIGQUIT  3
#define SIGINT   2
#define SIGPWR   30

#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

sighandler_t signal(int signum, sighandler_t handler);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signum);
int sigdelset(sigset_t* set, int signum);
int sigismember(const sigset_t* set, int signum);
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset);

#endif