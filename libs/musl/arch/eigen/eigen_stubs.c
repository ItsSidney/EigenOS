/* EigenOS musl port: stubs for subsystems that are out of scope for the
 * single-threaded, no-spawn ring-3 environment (threads, posix_spawn,
 * process priority, utsname, ioctl tty, the skipped strerror/strsignal,
 * and the timezone helpers normally in __tz.c).
 *
 * These let a full archive link cleanly. Programs that actually call a
 * stubbed function will get -ENOSYS/-1 at runtime; basic C programs do not
 * touch them. */

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <time.h>

/* set errno and return -1 (errno.h is the public header) */
static int eigen_set_errno(int e) { errno = e; return -1; }
#define __set_errno(e) eigen_set_errno(e)

/* ---- small decimal formatter (no libc dependency) ---- */
static char *eigen_itoa(int v, char *b)
{
	int neg = 0;
	if (v < 0) { neg = 1; v = -v; }
	char *p = b + 11;
	*p = 0;
	do { *--p = "0123456789"[v % 10]; v /= 10; } while (v);
	if (neg) *--p = '-';
	return p;
}

/* ---- skipped errno/strerror/strsignal ---- */
char *strerror(int e)
{
	static char buf[32];
	memcpy(buf, "Error ", 6);
	char *p = eigen_itoa(e, buf + 6);
	return buf + (p - buf);
}

char *strsignal(int s)
{
	static char buf[32];
	memcpy(buf, "Signal ", 7);
	char *p = eigen_itoa(s, buf + 7);
	return buf + (p - buf);
}

/* ---- timezone helpers (normally __tz.c): treat everything as UTC ---- */
const char __utc[4] = "UTC";
int __secs_to_zone(long long t, int local, int *isdst, long *offset,
                   long *oppoff, const char **zname)
{
	(void)t; (void)local;
	if (isdst) *isdst = 0;
	if (offset) *offset = 0;
	if (oppoff) *oppoff = 0;
	if (zname) *zname = "UTC";
	return 0;
}
const char *__tm_to_tzname(const struct tm *tm)
{
	(void)tm;
	return "UTC";
}
void __tzset(void) {}

/* ---- stdio seek (skipped __stdio_seek.c due to macro quirk) ---- */
#include "stdio_impl.h"
off_t __stdio_seek(FILE *f, off_t off, int whence)
{
	return syscall(SYS_lseek, f->fd, off, whence);
}

/* ---- threads / spawn / process (unsupported) ---- */
struct __ptcb;
int __clone(int (*fn)(void *), void *stack, int flags, void *arg, ...)
{
	(void)fn; (void)stack; (void)flags; (void)arg;
	return __set_errno(ENOSYS);
}
void __synccall(void (*func)(void *), void *arg)
{
	(void)func; (void)arg;
}
void __reset_tls(void) {}
void _pthread_cleanup_push(struct __ptcb *cb, void (*fn)(void *), void *arg)
{
	(void)cb; (void)fn; (void)arg;
}
void _pthread_cleanup_pop(struct __ptcb *cb, int execute)
{
	(void)cb; (void)execute;
}
int pthread_create(pthread_t *t, const pthread_attr_t *a,
                   void *(*fn)(void *), void *arg)
{
	(void)t; (void)a; (void)fn; (void)arg;
	return ENOSYS;
}
int pthread_join(pthread_t t, void **r)
{
	(void)t; (void)r;
	return ENOSYS;
}
int pthread_detach(pthread_t t) { (void)t; return ENOSYS; }
int pthread_attr_init(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int s)
{
	(void)a; (void)s;
	return 0;
}
int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *a, unsigned c)
{
	(void)b; (void)a; (void)c;
	return ENOSYS;
}
int pthread_barrier_wait(pthread_barrier_t *b) { (void)b; return ENOSYS; }
int pthread_barrier_destroy(pthread_barrier_t *b) { (void)b; return 0; }
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
	(void)how; (void)set; (void)old;
	return 0;
}
int pthread_setcancelstate(int s, int *o) { (void)s; (void)o; return 0; }

int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
	(void)how; (void)set; (void)old;
	return 0;
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *attr,
                char *const argv[], char *const envp[])
{
	(void)pid; (void)path; (void)fa; (void)attr; (void)argv; (void)envp;
	return ENOSYS;
}
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *a)
{
	(void)a;
	return 0;
}
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *a)
{
	(void)a;
	return 0;
}
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *a, int fd)
{
	(void)a; (void)fd;
	return 0;
}
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *a, int s, int t)
{
	(void)a; (void)s; (void)t;
	return 0;
}

int uname(struct utsname *buf)
{
	if (buf) memset(buf, 0, sizeof *buf);
	return 0;
}
int ioctl(int fd, int req, ...)
{
	(void)fd; (void)req;
	return __set_errno(ENOSYS);
}
int setpriority(int which, id_t who, int prio)
{
	(void)which; (void)who; (void)prio;
	return __set_errno(ENOSYS);
}
int getpriority(int which, id_t who)
{
	(void)which; (void)who;
	return __set_errno(ENOSYS);
}

/* ---- thread-list / PTC bridges (full pthreads not yet wired) ---- */
int __thread_list_lock;
void __inhibit_ptc(void) {}
void __release_ptc(void) {}
#include <errno.h>
#include <sys/types.h>
int getgrouplist(const char* user, gid_t group, gid_t* groups, int* ngroups) {
    (void)user; (void)group;
    if (!ngroups) return -1;
    if (*ngroups > 0 && groups) groups[0] = group;
    *ngroups = 1;
    return 1;
}
#include <sys/mman.h>
int mlock(const void* addr, size_t len) { (void)addr; (void)len; return 0; }
int munlock(const void* addr, size_t len) { (void)addr; (void)len; return 0; }
#include <setjmp.h>
extern int __sigsetjmp(sigjmp_buf buf, int savemask);
int sigsetjmp(sigjmp_buf buf, int savemask) {
    return __sigsetjmp(buf, savemask);
}
