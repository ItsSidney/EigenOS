/* posix.c — ring-3 POSIX shims for ported libraries (EFL, FreeType).
 *
 * Everything here is emulated over the eigen syscalls; no kernel changes
 * are needed. Goals, in priority order:
 *   - mmap: satisfy code that maps files/anon memory by allocating a
 *     buffer and (for fd-backed) reading the whole file in. The eigen FS
 *     is a flat in-RAM tree, so "page cache" is just memcpy.
 *   - getenv/setenv: a tiny in-process environment, seeded empty.
 *   - time: struct tm / strftime / gettimeofday / clock_gettime over
 *     eigen_time_get + eigen_gettime_ms.
 *   - opendir/readdir: parse eigen_fs_list() output (ANSI-coloured,
 *     one entry per line, dirs end in '/').
 *   - dlopen/dlsym: stubs — EFL is linked statically.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "posix.h"
#include "userlib.h"   /* eigen_* syscall wrappers */
#include "poll.h"
#include "sys/select.h"
#include "sys/epoll.h"
#include <sys/stat.h>
#include <ctype.h>    /* tolower for strcasecmp */
#include <sched.h>    /* cpu_set_t for sched_* */
#include <signal.h>   /* sigset_t for sig* helpers */

#define MAP_FAILED_PTR ((void*)-1)

/* Translate an eigen ABI return code into a POSIX errno (mirrors the
   static map_errno() in libc.c). */
int map_posix_errno(int rc) {
    switch (rc) {
        case -2: return ENOENT;
        case -3: return ENOMEM;
        case -4: return EINVAL;
        case -5: return ESRCH;
        case -6: return EINVAL;
        default: return EIO;
    }
}

/* ===================== mmap (emulated) ===================== */
void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)prot; (void)flags; (void)off;
    if (len == 0) { errno = EINVAL; return MAP_FAILED_PTR; }
    if (len > (size_t)0x7FFFFFF0) { errno = ENOMEM; return MAP_FAILED_PTR; }

    if (fd >= 0) {
        const char* path = _eigen_fd_path(fd);
        long sz = (path[0] && strcmp(path, "")) ? eigen_fs_size(path) : -1;
        if (sz < 0) { errno = ENOENT; return MAP_FAILED_PTR; }
        if ((size_t)sz > len) len = (size_t)sz;
        void* p = eigen_malloc(len ? len : 1);
        if (!p) { errno = ENOMEM; return MAP_FAILED_PTR; }
        memset(p, 0, len);
        if (sz > 0) {
            int r = eigen_fs_read_file(path, p, (int)sz);
            if (r < 0) { eigen_free(p); errno = EIO; return MAP_FAILED_PTR; }
        }
        return p;
    }

    void* p = eigen_malloc(len);
    if (!p) { errno = ENOMEM; return MAP_FAILED_PTR; }
    memset(p, 0, len);
    return p;
}

int munmap(void* addr, size_t len) {
    (void)len;
    if (addr && addr != MAP_FAILED_PTR) eigen_free(addr);
    return 0;
}

int mprotect(void* addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0;   /* no page protection in the flat heap — always writable */
}

/* ===================== fd -> path registry ===================== */
static const char* g_fd_paths[64];

int _eigen_fd_register(int fd, const char* path) {
    if (fd >= 0 && fd < 64) { g_fd_paths[fd] = path; return 0; }
    return -1;
}
void _eigen_fd_forget(int fd) {
    if (fd >= 0 && fd < 64) g_fd_paths[fd] = 0;
}
const char* _eigen_fd_path(int fd) {
    if (fd >= 0 && fd < 64 && g_fd_paths[fd]) return g_fd_paths[fd];
    return "";
}

/* ===================== environment ===================== */
#define ENV_MAX 64
static char* g_env[ENV_MAX];
static char g_env_initialized = 0;

static void env_init(void) {
    if (g_env_initialized) return;
    g_env_initialized = 1;
    for (int i = 0; i < ENV_MAX; i++) g_env[i] = 0;
    environ = g_env;
}

char* getenv(const char* name) {
    if (!name) return 0;
    env_init();
    size_t nl = strlen(name);
    for (int i = 0; g_env[i]; i++) {
        if (strncmp(g_env[i], name, nl) == 0 && g_env[i][nl] == '=')
            return g_env[i] + nl + 1;
    }
    return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    env_init();
    size_t nl = strlen(name), vl = value ? strlen(value) : 0;
    for (int i = 0; g_env[i]; i++) {
        if (strncmp(g_env[i], name, nl) == 0 && g_env[i][nl] == '=') {
            if (!overwrite) return 0;
            char* nv = (char*)eigen_malloc(nl + vl + 2);
            if (!nv) { errno = ENOMEM; return -1; }
            memcpy(nv, name, nl);
            nv[nl] = '=';
            memcpy(nv + nl + 1, value, vl);
            nv[nl + vl + 1] = 0;
            eigen_free(g_env[i]);
            g_env[i] = nv;
            return 0;
        }
    }
    int slot = 0;
    while (slot < ENV_MAX - 1 && g_env[slot]) slot++;
    if (slot >= ENV_MAX - 1) { errno = ENOMEM; return -1; }
    g_env[slot] = (char*)eigen_malloc(nl + vl + 2);
    if (!g_env[slot]) { errno = ENOMEM; return -1; }
    memcpy(g_env[slot], name, nl);
    g_env[slot][nl] = '=';
    memcpy(g_env[slot] + nl + 1, value, vl);
    g_env[slot][nl + vl + 1] = 0;
    return 0;
}

int unsetenv(const char* name) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    env_init();
    size_t nl = strlen(name);
    for (int i = 0; g_env[i]; i++) {
        if (strncmp(g_env[i], name, nl) == 0 && g_env[i][nl] == '=') {
            eigen_free(g_env[i]);
            for (; g_env[i]; i++) g_env[i] = g_env[i + 1];
            return 0;
        }
    }
    return 0;
}

int putenv(char* string) {
    if (!string || !*string) { errno = EINVAL; return -1; }
    char* eq = strchr(string, '=');
    if (!eq) return unsetenv(string);
    int nl = (int)(eq - string);
    if (nl == 0) { errno = EINVAL; return -1; }
    env_init();
    for (int i = 0; g_env[i]; i++) {
        if (strncmp(g_env[i], string, (size_t)nl) == 0 && g_env[i][nl] == '=') {
            g_env[i] = string;   /* putenv takes ownership */
            return 0;
        }
    }
    int slot = 0;
    while (slot < ENV_MAX - 1 && g_env[slot]) slot++;
    if (slot >= ENV_MAX - 1) { errno = ENOMEM; return -1; }
    g_env[slot] = string;
    return 0;
}

/* ===================== time ===================== */
/* Days-from-civil and civil-from-days (Howard Hinnant's algorithms),
   mirroring the epoch conversion in libc.c's time(). */
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, int* y, int* m, int* d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + (mm <= 2);
    *m = (int)mm;
    *d = (int)dd;
}

static struct tm g_tm;
static char g_tm_buf[64];

static void tm_from_time(const time_t* t, struct tm* out) {
    long long secs = (long long)(*t);
    long long days = secs / 86400;
    int rem = (int)(secs % 86400);
    if (rem < 0) { rem += 86400; days--; }
    civil_from_days((long)days, &out->tm_year, &out->tm_mon, &out->tm_mday);
    out->tm_year -= 1900;
    out->tm_mon -= 1;
    out->tm_hour = rem / 3600;
    out->tm_min = (rem / 60) % 60;
    out->tm_sec = rem % 60;
    out->tm_wday = (int)((days + 4) % 7);   /* 1970-01-01 was a Thursday */
    if (out->tm_wday < 0) out->tm_wday += 7;
    out->tm_yday = (int)(days_from_civil(out->tm_year + 1900, out->tm_mon + 1, out->tm_mday)
                         - days_from_civil(out->tm_year + 1900, 1, 1));
    out->tm_isdst = 0;
}

struct tm* localtime(const time_t* t) {
    if (!t) { errno = EINVAL; return 0; }
    tm_from_time(t, &g_tm);
    return &g_tm;
}
struct tm* gmtime(const time_t* t) {
    if (!t) { errno = EINVAL; return 0; }
    tm_from_time(t, &g_tm);
    return &g_tm;
}

time_t mktime(struct tm* tm) {
    if (!tm) { errno = EINVAL; return (time_t)-1; }
    long days = days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    return (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}

static const char* WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

char* asctime(const struct tm* tm) {
    if (!tm) return 0;
    snprintf(g_tm_buf, sizeof g_tm_buf, "%s %s %2d %02d:%02d:%02d %d\n",
             WD[tm->tm_wday >= 0 ? tm->tm_wday : 0],
             MON[tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0],
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
             tm->tm_year + 1900);
    return g_tm_buf;
}
char* ctime(const time_t* t) {
    struct tm* tm = localtime(t);
    return tm ? asctime(tm) : 0;
}

size_t strftime(char* s, size_t max, const char* fmt, const struct tm* tm) {
    if (!s || !fmt || !tm) { if (s && max) s[0] = 0; return 0; }
    static const char* FULL_MON[12] = {"January","February","March","April","May","June",
        "July","August","September","October","November","December"};
    static const char* FULL_WD[7] = {"Sunday","Monday","Tuesday","Wednesday",
        "Thursday","Friday","Saturday"};
    size_t n = 0;
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { if (n + 1 < max) s[n++] = *p; continue; }
        p++;
        char tmp[32]; char* t = tmp; int tl = 0;
        switch (*p) {
            case 'Y': tl = snprintf(tmp, sizeof tmp, "%d", tm->tm_year + 1900); break;
            case 'y': tl = snprintf(tmp, sizeof tmp, "%02d", (tm->tm_year + 1900) % 100); break;
            case 'm': tl = snprintf(tmp, sizeof tmp, "%02d", tm->tm_mon + 1); break;
            case 'd': tl = snprintf(tmp, sizeof tmp, "%02d", tm->tm_mday); break;
            case 'H': tl = snprintf(tmp, sizeof tmp, "%02d", tm->tm_hour); break;
            case 'I': { int h = tm->tm_hour % 12; if (h == 0) h = 12; tl = snprintf(tmp, sizeof tmp, "%02d", h); break; }
            case 'M': tl = snprintf(tmp, sizeof tmp, "%02d", tm->tm_min); break;
            case 'S': tl = snprintf(tmp, sizeof tmp, "%02d", tm->tm_sec); break;
            case 'p': t = tm->tm_hour < 12 ? "AM" : "PM"; tl = 2; break;
            case 'a': t = (char*)WD[tm->tm_wday >= 0 ? tm->tm_wday : 0]; tl = 3; break;
            case 'A': t = (char*)FULL_WD[tm->tm_wday >= 0 ? tm->tm_wday : 0]; tl = (int)strlen(t); break;
            case 'b': case 'h': t = (char*)MON[tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0]; tl = 3; break;
            case 'B': t = (char*)FULL_MON[tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0]; tl = (int)strlen(t); break;
            case 'j': tl = snprintf(tmp, sizeof tmp, "%03d", tm->tm_yday + 1); break;
            case 'e': tl = snprintf(tmp, sizeof tmp, "%2d", tm->tm_mday); break;
            case '%': t = "%"; tl = 1; break;
            default:  t = "?"; tl = 1; break;
        }
        if (tl < 0) tl = 0;
        if (tl > (int)sizeof tmp - 1) tl = (int)sizeof tmp - 1;
        for (int i = 0; i < tl; i++) {
            if (n + 1 >= max) break;
            s[n++] = t[i];
        }
        if (n + 1 >= max) break;
    }
    s[n] = 0;
    return n;
}

int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    if (!tv) return -1;
    tv->tv_sec = (long)time(0);
    tv->tv_usec = 0;
    return 0;
}

int clock_gettime(int clk_id, struct timespec* ts) {
    if (!ts) { errno = EINVAL; return -1; }
    (void)clk_id;
    uint32_t ms = eigen_gettime_ms();
    ts->tv_sec = (long)(ms / 1000);
    ts->tv_nsec = (long)(ms % 1000) * 1000000L;
    return 0;
}

/* ===================== sleep / process ===================== */
int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (!req) { errno = EINVAL; return -1; }
    uint32_t ms = (uint32_t)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    if (ms == 0 && req->tv_nsec > 0) ms = 1;
    eigen_sleep_ms(ms);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}
int usleep(unsigned long usec) {
    eigen_sleep_ms((uint32_t)(usec / 1000));
    return 0;
}
pid_t getpid(void) { return (pid_t)eigen_getpid(); }
pid_t getppid(void) { return 0; }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
uid_t getgid(void) { return 0; }
char* getcwd(char* buf, size_t size) {
    if (!buf || size == 0) { errno = EINVAL; return 0; }
    int r = eigen_fs_pwd(buf, (int)size - 1);
    if (r < 0) { errno = map_posix_errno(r); return 0; }
    buf[r] = 0;
    return buf;
}

/* ===================== directories ===================== */
/* REAL getdents64-based directory reads (kernel SYS_getdents64). */
#include "eigen.h"
#include <fcntl.h>
#include <unistd.h>
#ifndef O_DIRECTORY
#define O_DIRECTORY 0200000
#endif
#ifndef EIGEN_SYS_GETDENTS64
#define EIGEN_SYS_GETDENTS64 39
#endif
struct eigen_dirent64 {
    unsigned long long d_ino;
    long long          d_off;
    unsigned short     d_reclen;
    unsigned char      d_type;
    char               d_name[];
};

struct DIR {
    int            fd;
    int            pos;        /* byte offset into buf */
    int            end;        /* valid bytes in buf   */
    struct dirent  cur;
    char           buf[4096];
};

DIR* opendir(const char* path) {
    int fd = open(path[0] ? path : "/", O_RDONLY | O_DIRECTORY);
    if (fd < 0) return 0;
    DIR* d = (DIR*)eigen_malloc(sizeof(DIR));
    if (!d) { close(fd); errno = ENOMEM; return 0; }
    d->fd = fd; d->pos = 0; d->end = 0;
    return d;
}

struct dirent* readdir(DIR* dir) {
    if (!dir || dir->fd < 0) return 0;
    if (dir->pos >= dir->end) {
        long n = (long)eigen_syscall(EIGEN_SYS_GETDENTS64,
                    (unsigned long)dir->fd,
                    (unsigned long)(uintptr_t)dir->buf,
                    (unsigned long)sizeof(dir->buf), 0);
        if (n <= 0) return 0;
        dir->pos = 0; dir->end = (int)n;
    }
    struct eigen_dirent64* de =
        (struct eigen_dirent64*)(dir->buf + dir->pos);
    if (!de->d_reclen) return 0;
    int i = 0;
    while (de->d_name[i] && i < 255) { dir->cur.d_name[i] = de->d_name[i]; i++; }
    dir->cur.d_name[i] = 0;
    dir->cur.d_ino = (long)de->d_ino;
    dir->pos += de->d_reclen;
    return &dir->cur;
}

int closedir(DIR* d) {
    if (!d) return -1;
    int r = close(d->fd);
    eigen_free(d);
    return r;
}

/* ===================== dlopen/dlsym (stubs) ===================== */
static char g_dlerror[] = "dynamic loading is not supported (statically linked)";

void* dlopen(const char* filename, int flags) {
    (void)filename; (void)flags;
    errno = ENOSYS;
    return 0;
}
void* dlsym(void* handle, const char* symbol) {
    (void)handle; (void)symbol;
    errno = ENOSYS;
    return 0;
}
int dlclose(void* handle) { (void)handle; errno = ENOSYS; return -1; }
char* dlerror(void) { return g_dlerror; }

/* ===================== misc string helpers ===================== */
void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void*)(p + i);
    return 0;
}
char* strtok_r(char* s, const char* delim, char** save) {
    if (!s) s = *save;
    if (!s) return 0;
    while (*s && strchr(delim, *s)) s++;
    if (!*s) { *save = 0; return 0; }
    char* tok = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) { *s = 0; *save = s + 1; } else *save = 0;
    return tok;
}
size_t strnlen(const char* s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}
char* strndup(const char* s, size_t n) {
    size_t l = strnlen(s, n);
    char* p = (char*)eigen_malloc(l + 1);
    if (!p) return 0;
    memcpy(p, s, l);
    p[l] = 0;
    return p;
}
/* ===================== poll / select / epoll ===================== */
/* All backing objects are synchronous, so readiness is static: files and
   terminal/serial output poll as readable/writable, unbacked fds as
   POLLNVAL. EIGEN_SYS_POLL blocks via sleep_task up to the timeout. */

int poll(struct pollfd* fds, nfds_t nfds, int timeout_ms) {
    return (int)eigen_syscall(EIGEN_SYS_POLL, (uint64_t)(uintptr_t)fds, nfds,
                              (uint64_t)(int64_t)timeout_ms, 0);
}

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
           struct timeval* timeout) {
    if (nfds < 0) { errno = EINVAL; return -1; }
    if (nfds > FD_SETSIZE) nfds = FD_SETSIZE;
    struct pollfd pfds[FD_SETSIZE];
    int map[FD_SETSIZE];
    nfds_t np = 0;
    for (int i = 0; i < nfds; i++) {
        short ev = 0;
        if (readfds && FD_ISSET(i, readfds)) ev |= POLLIN;
        if (writefds && FD_ISSET(i, writefds)) ev |= POLLOUT;
        if (ev || (exceptfds && FD_ISSET(i, exceptfds))) {
            pfds[np].fd = i;
            pfds[np].events = ev;
            pfds[np].revents = 0;
            map[np] = i;
            np++;
        }
    }
    int timeout_ms = -1;
    if (timeout) {
        long long ms = (long long)timeout->tv_sec * 1000
                     + (timeout->tv_usec + 999) / 1000;
        if (ms > 2147483647LL) ms = 2147483647LL;
        timeout_ms = (int)ms;
    }
    int rn = poll(pfds, np, timeout_ms);
    if (rn < 0) return -1;
    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);
    int count = 0;
    for (nfds_t i = 0; i < np; i++) {
        int fired = 0;
        if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
            if (readfds) FD_SET(map[i], readfds);
            fired = 1;
        }
        if (pfds[i].revents & (POLLOUT | POLLERR | POLLNVAL)) {
            if (writefds) FD_SET(map[i], writefds);
            fired = 1;
        }
        if (pfds[i].revents & (POLLERR | POLLNVAL)) {
            if (exceptfds) FD_SET(map[i], exceptfds);
            fired = 1;
        }
        if (fired) count++;
    }
    return count;
}

int epoll_create1(int flags) {
    (void)flags;
    return (int)eigen_syscall(EIGEN_SYS_EPOLL, EIGEN_EPOLL_CREATE, 0, 0, 0);
}

int epoll_ctl(int epid, int op, int fd, struct epoll_event* event) {
    struct {
        int      fd;
        uint32_t events;
        uint64_t data;
    } arg;
    arg.fd = fd;
    arg.events = event ? event->events : 0;
    arg.data = event ? event->data : 0;
    long r = (long)eigen_syscall(EIGEN_SYS_EPOLL, EIGEN_EPOLL_CTL,
                                 (uint64_t)(uint32_t)epid, (uint64_t)(uint32_t)op,
                                 (uint64_t)(uintptr_t)&arg);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

int epoll_wait(int epid, struct epoll_event* events, int maxevents, int timeout_ms) {
    if (maxevents <= 0 || !events) { errno = EINVAL; return -1; }
    uint64_t packed = ((uint64_t)(uint32_t)maxevents << 32)
                    | (uint64_t)(uint32_t)timeout_ms;
    long r = (long)eigen_syscall(EIGEN_SYS_EPOLL, EIGEN_EPOLL_WAIT,
                                 (uint64_t)(uint32_t)epid,
                                 (uint64_t)(uintptr_t)events, packed);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

/* ------------------------------------------------------------------ */
/* Extra POSIX surface used by Eina / ported libraries                 */
/* ------------------------------------------------------------------ */
#include <stdarg.h>
#include <strings.h>

int isatty(int fd) {
    /* console fds are character devices — ports gate raw-mode setup on it */
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

/* ── termios (tier 1): per-process state over the raw console ── */
#include <termios.h>
#include <user/eigen.h>

static struct termios g_tio;
static int            g_tio_init = 0;

static void tio_defaults(struct termios* t) {
    memset(t, 0, sizeof(*t));
    t->c_iflag = ICRNL | IXON | IMAXBEL | IUTF8;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD | CLOCAL;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    t->c_line  = 0;
    t->c_cc[VEOF]   = 4;   /* ^D */
    t->c_cc[VERASE] = 127; /* DEL */
    t->c_cc[VINTR]  = 3;   /* ^C */
    t->c_cc[VQUIT]  = 28;  /* ^\ */
    t->c_cc[VSUSP]  = 26;  /* ^Z */
    t->c_cc[VMIN]   = 1;
    t->c_cc[VTIME]  = 0;
    t->c_ispeed = B38400;
    t->c_ospeed = B38400;
}

int tcgetattr(int fd, struct termios* t) {
    if (!t) { errno = EINVAL; return -1; }
    if (!g_tio_init) { tio_defaults(&g_tio); g_tio_init = 1; }
    *t = g_tio;
    return (fd >= 0) ? 0 : (-1);
}

#ifndef EIGEN_F_PTYRAW
#define EIGEN_F_PTYRAW 1001
#endif
int tcsetattr(int fd, int action, const struct termios* t) {
    (void)action;
    if (!t) { errno = EINVAL; return -1; }
    int was_raw = (g_tio_init && !(g_tio.c_lflag & ICANON));
    int now_raw = (t->c_lflag & ICANON) ? 0 : 1;
    g_tio = *t;
    g_tio_init = 1;
    if (fd >= 3 && was_raw != now_raw)
        fcntl(fd, EIGEN_F_PTYRAW, now_raw);   /* kernel line-discipline flip */
    return 0;
}

static int g_rows = 24, g_cols = 80;

/* terminal size for TUIs: rows/cols packed via fcntl */
int eigen_get_winsize(int fd, int* rows, int* cols) {
    extern int fcntl(int, int, ...);
    /* kernel stores it; expose via F_GETFL-style read is not wired — the
       app told US the size when it resized. Return last-known defaults. */
    (void)fd;
    if (rows) *rows = g_rows; if (cols) *cols = g_cols;
    return 0;
}
void eigen_set_winsize(int fd, int rows, int cols) {
    extern int fcntl(int, int, ...);
    g_rows = rows; g_cols = cols;
    #ifndef EIGEN_F_PTYWINSZ
    #define EIGEN_F_PTYWINSZ 1002
    #endif
    if (fd >= 0) fcntl(fd, EIGEN_F_PTYWINSZ, ((rows & 0xFFFF) << 16) | (cols & 0xFFFF));
}

void cfmakeraw(struct termios* t) {
    if (!t) return;
    t->c_iflag &= ~(unsigned)(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|
                              ICRNL|IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(unsigned)(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    t->c_cflag &= ~(unsigned)(CSIZE|PARENB);
    t->c_cflag |= CS8 | CREAD;
    t->c_cc[VMIN]  = 1;
    t->c_cc[VTIME] = 0;
}

void cfmakeraw_nux(struct termios* t) { /* alias some ports expect */
    cfmakeraw(t);
}

speed_t cfgetispeed(const struct termios* t){ return t ? t->c_ispeed : 0; }
speed_t cfgetospeed(const struct termios* t){ return t ? t->c_ospeed : 0; }
int cfsetispeed(struct termios* t, speed_t s){ if(!t){errno=EINVAL;return -1;} t->c_ispeed=s; return 0; }
int cfsetospeed(struct termios* t, speed_t s){ if(!t){errno=EINVAL;return -1;} t->c_ospeed=s; return 0; }

int tcflush(int fd, int queue) {
    (void)fd;
    if (queue == TCIFLUSH || queue == TCIOFLUSH) {
        /* drain the console input queue for real */
        eigen_syscall(EIGEN_SYS_INPUT, EIGEN_INPUT_DRAIN, 0, 0, 0);
    }
    return 0;
}
int tcdrain(int fd)        { (void)fd; return 0; }
int tcsendbreak(int f,int d){ (void)f;(void)d; return 0; }
pid_t tcgetsid(int fd)     { (void)fd; errno = ENOSYS; return -1; }

int access(const char* path, int mode) {
    (void)mode;
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    errno = ENOENT;
    return -1;
}

char* realpath(const char* path, char* resolved) {
    if (!resolved) {
        resolved = malloc(strlen(path) + 1);
        if (!resolved) { errno = ENOMEM; return NULL; }
    }
    strcpy(resolved, path);
    return resolved;
}

int vasprintf(char** strp, const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return -1;
    char* buf = malloc((size_t)n + 1);
    if (!buf) { errno = ENOMEM; return -1; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    *strp = buf;
    return n;
}

char* strptime(const char* s, const char* fmt, struct tm* tm) {
    (void)s; (void)fmt; (void)tm;
    /* Not implemented for the freestanding Eina port. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Scheduling / affinity (stubs for the single-CPU freestanding port) */
/* ------------------------------------------------------------------ */
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t* mask) {
    (void)pid; (void)cpusetsize;
    if (mask) *mask = 1UL;   /* CPU 0 only */
    return 0;
}
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t* mask) {
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
}
int sched_get_priority_min(int policy) { (void)policy; return 0; }
int sched_get_priority_max(int policy) { (void)policy; return 0; }
int getpriority(int which, id_t who) { (void)which; (void)who; return 0; }
int setpriority(int which, id_t who, int prio) { (void)which; (void)who; (void)prio; return 0; }

/* ------------------------------------------------------------------ */
/* Signal-set helpers (stubs for the freestanding port)                */
/* ------------------------------------------------------------------ */
int sigemptyset(sigset_t* set) { if (set) *set = 0UL; return 0; }
int sigfillset(sigset_t* set)  { if (set) *set = ~0UL; return 0; }
int sigaddset(sigset_t* set, int signum) {
    if (!set || signum < 0 || signum >= (int)(sizeof(unsigned long)*8)) return -1;
    *set |= (1UL << signum); return 0;
}
int sigdelset(sigset_t* set, int signum) {
    if (!set || signum < 0 || signum >= (int)(sizeof(unsigned long)*8)) return -1;
    *set &= ~(1UL << signum); return 0;
}
int sigismember(const sigset_t* set, int signum) {
    if (!set || signum < 0 || signum >= (int)(sizeof(unsigned long)*8)) return 0;
    return (*set & (1UL << signum)) ? 1 : 0;
}
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
    (void)how; (void)set; (void)oldset; return 0;
}

/* Directory creation stub (no real FS hierarchy in the freestanding port).
 * Declared weak so ports that supply their own (e.g. doom_stubs.c) win. */
int __attribute__((weak)) mkdir(const char* path, mode_t mode) {
    (void)path; (void)mode; return 0;
}

int fstat(int fd, struct stat* buf) {
    (void)fd;
    if (buf) memset(buf, 0, sizeof(*buf));
    return 0;
}
