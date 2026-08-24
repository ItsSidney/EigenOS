/* libc.c — shared ring-3 freestanding libc for all user apps.
 *
 * Eigen's userlib (userlib.o) provides only the ring-3 SYSCALL wrappers
 * (eigen_malloc, eigen_win_create, ...). It does NOT bundle a libc, so
 * this file supplies everything a ported app needs:
 *   string / stdlib / stdio / ctype / math  + sscanf/qsort/strdup
 *
 * Include order puts src/user/lib/libc/inc FIRST, so our <stdio.h>/<stdlib.h>
 * etc. prototypes win over anything else.
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <wctype.h>
#include <signal.h>
#include <user/eigen.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <math.h>

#include "userlib.h"   /* eigen_malloc/free, eigen_write, eigen_gettime_ms, ... */
#include "posix.h"     /* mmap/env/time/dirent/dlopen shims implemented in posix.c */

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ----- memory ----- */
void* memcpy(void* d, const void* s, size_t n) {
    const unsigned char* src = (const unsigned char*)s;
    unsigned char* dst = (unsigned char*)d;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return d;
}
void* memmove(void* d, const void* s, size_t n) {
    const unsigned char* src = (const unsigned char*)s;
    unsigned char* dst = (unsigned char*)d;
    if (dst < src) { for (size_t i = 0; i < n; i++) dst[i] = src[i]; }
    else { for (size_t i = n; i > 0; i--) dst[i-1] = src[i-1]; }
    return d;
}
void* memset(void* p, int c, size_t n) {
    unsigned char* b = (unsigned char*)p;
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)c;
    return p;
}
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
/* Note: memchr is implemented in posix.c (not here) to avoid a
 * duplicate-definition clash when both libc.o and posix.o link into
 * an app. */

/* ----- strings ----- */
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) break;
    }
    return 0;
}
char* strcpy(char* d, const char* s) { char* r = d; while ((*d++ = *s++)); return r; }
char* strncpy(char* d, const char* s, size_t n) {
    size_t i = 0;
    while (i < n && s[i]) { d[i++] = s[i]; }
    while (i < n) { d[i++] = 0; }
    return d;
}
char* strcat(char* d, const char* s) { char* r = d; while (*r) r++; while ((*r++ = *s++)); return d; }
char* strncat(char* d, const char* s, size_t n) {
    char* r = d; while (*r) r++;
    for (size_t i = 0; i < n && s[i]; i++) *r++ = s[i];
    *r = 0; return d;
}
char* strchr(const char* s, int c) { while (*s) { if (*s == (char)c) return (char*)s; s++; } if (c==0) return (char*)s; return 0; }
char* strrchr(const char* s, int c) {
    const char* last = 0;
    while (*s) { if (*s == (char)c) last = s; s++; }
    if (c == 0) return (char*)s;
    return (char*)last;
}
 char* index(const char* s, int c)  { return strchr(s, c); }
 char* rindex(const char* s, int c) { return strrchr(s, c); }
 char* strpbrk(const char* s, const char* accept) {
     for (; *s; s++) { for (const char* a = accept; *a; a++) if (*s == *a) return (char*)s; }
     return 0;
 }
char* strstr(const char* h, const char* n) {
    if (!*n) return (char*)h;
    for (; *h; h++) {
        const char* a = h; const char* b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return 0;
}
char* strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)eigen_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
int strcasecmp(const char* a, const char* b) {
    while (*a && ((*a == *b) || ((*a|0x20) == (*b|0x20)))) { a++; b++; }
    int ca = *a, cb = *b; if (ca>='A'&&ca<='Z') ca+=32; if (cb>='A'&&cb<='Z') cb+=32;
    return ca - cb;
}
int strncasecmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = a[i], cb = b[i];
        if (ca>='A'&&ca<='Z') ca+=32; if (cb>='A'&&cb<='Z') cb+=32;
        if (ca != cb || ca == 0) return ca - cb;
    }
    return 0;
}
void bzero(void* s, size_t n) { memset(s, 0, n); }

/* ----- stdlib ----- */
void* malloc(size_t sz) { return eigen_malloc(sz ? sz : 1); }
void  free(void* p) { if (p) eigen_free(p); }
void* calloc(size_t n, size_t sz) {
    size_t t = n * sz; void* p = eigen_malloc(t ? t : 1);
    if (p) memset(p, 0, t);
    return p;
}
void* realloc(void* p, size_t sz) {
    if (!sz) { free(p); return 0; }
    if (!p) return malloc(sz);
    void* n = malloc(sz);
    if (n) memcpy(n, p, sz);
    free(p);
    return n;
}
int atoi(const char* s) {
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}
long atol(const char* s) { return (long)atoi(s); }

long strtol(const char* s, char** end, int base) {
    const char* start = s;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    if (base == 0) {
        base = 10;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    long v = 0;
    int any = 0;
    for (;; s++) {
        int d;
        if      (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        any = 1;
    }
    if (!any) s = start;
    if (end) *end = (char*)s;
    return neg ? -v : v;
}
void exit(int code) { eigen_exit(code); for (;;) {} }
void abort(void) { eigen_exit(-1); for (;;) {} }

void __assert_fail(const char* expr, const char* file, int line) {
    fprintf(&_eigen_stderr, "Assertion failed: %s at %s:%d\n", expr, file, line);
    abort();
}
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
static unsigned long g_rand = 1;
void srand(unsigned seed) { g_rand = seed ? seed : 1; }
int rand(void) { g_rand = g_rand * 1103515245 + 12345; return (int)((g_rand >> 16) & 0x7FFF); }

void srandom(unsigned long seed) { srand((unsigned)seed); }
long random(void) { return (long)rand(); }

/* awk's main() calls envinit(environ) at startup; give it a valid
   (empty) environment instead of NULL. */
static char* environ_stub[] = { 0 };
char** environ = environ_stub;
char* setlocale(int category, const char* locale) {
    (void)category; (void)locale;
    return "C";
}
int system(const char* cmd) { (void)cmd; errno = ENOSYS; return -1; }

sighandler_t signal(int signum, sighandler_t handler) {
    long prev = (long)eigen_syscall(EIGEN_SYS_SIGNAL,
                                    (unsigned long)signum,
                                    (unsigned long)(uintptr_t)handler, 0, 0);
    return (prev == (long)-1) ? SIG_ERR : (sighandler_t)(uintptr_t)prev;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    long r = (long)eigen_syscall(EIGEN_SYS_WAIT4, (unsigned long)pid,
                                 (unsigned long)(uintptr_t)status,
                                 (unsigned long)options, 0);
    if (r < 0) { errno = ECHILD; return -1; }
    return (pid_t)r;
}

pid_t wait(int* status) { return waitpid(-1, status, 0); }

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    int arg = va_arg(ap, int);
    va_end(ap);
    int r = eigen_fcntl(fd, cmd, arg);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}

time_t time(time_t* out) {
    int rtc[6];
    if (eigen_time_get(rtc) != 0) {
        if (out) *out = 0;
        return 0;
    }
    int y = rtc[5], mo = rtc[4], d = rtc[3];
    y -= mo <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = (long long)era * 146097 + doe - 719468;
    long long secs = days * 86400 + (long long)rtc[0] * 3600
                   + (long long)rtc[1] * 60 + rtc[2];
    if (out) *out = (time_t)secs;
    return (time_t)secs;
}

/* ASCII mbtowc/wctomb: single-byte identity, so awk's case-conversion
   and multibyte counting behave like a C locale. */
int mbtowc(wchar_t* pwc, const char* s, size_t n) {
    if (s == NULL) return 0;
    if (n == 0) return -1;
    if (pwc) *pwc = (unsigned char)*s;
    return 1;
}

int wctomb(char* s, wchar_t wc) {
    if (s == NULL) return 0;
    if (wc < 0 || wc > 255) return -1;
    *s = (char)wc;
    return 1;
}

int iswspace(wint_t wc) { return (wc >= 0 && wc < 256) ? isspace(wc) : 0; }
int iswdigit(wint_t wc) { return (wc >= 0 && wc < 256) ? isdigit(wc) : 0; }
int iswalpha(wint_t wc) { return (wc >= 0 && wc < 256) ? isalpha(wc) : 0; }
int iswalnum(wint_t wc) { return (wc >= 0 && wc < 256) ? isalnum(wc) : 0; }
wint_t towupper(wint_t wc) { return (wc >= 0 && wc < 256) ? toupper(wc) : wc; }
wint_t towlower(wint_t wc) { return (wc >= 0 && wc < 256) ? tolower(wc) : wc; }

static void swap_b(void* a, void* b, size_t sz) {
    unsigned char* x=(unsigned char*)a, *y=(unsigned char*)b, t;
    for (size_t i=0;i<sz;i++){ t=x[i]; x[i]=y[i]; y[i]=t; }
}
void qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*,const void*)) {
    unsigned char* b=(unsigned char*)base;
    for (size_t i=1;i<nmemb;i++)
        for (size_t j=i;j>0;j--){
            if (cmp(b+(j-1)*size, b+j*size)>0) swap_b(b+(j-1)*size,b+j*size,size);
            else break;
        }
}
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*)) {
    const unsigned char* b=(const unsigned char*)base;
    size_t lo=0, hi=nmemb;
    while (lo<hi){ size_t mid=lo+(hi-lo)/2; int r=cmp(key,b+mid*size);
        if (r<0) hi=mid; else if (r>0) lo=mid+1; else return (void*)(b+mid*size); }
    return 0;
}

/* ----- stdio: real FILE* layer -----
   The kernel FS has no per-fd offsets (fs_read always reads from 0,
   fs_write always appends), so READ streams buffer the whole file in
   memory — trivial here since the FS is in-RAM anyway. That also makes
   fseek/ftell exact. Write streams write through to the fd. */
typedef struct _eigen_FILE {
    int  fd;                /* eigen fd; 1/2 = kernel log (stdout/stderr) */
    int  flags;             /* F_READ / F_WRITE / F_EOF / F_ERR           */
    unsigned char* data;    /* read streams: whole file content           */
    long size, pos;
    int  unget;             /* pushback char from ungetc; EOF = empty     */
} FILE;

enum { F_READ = 1, F_WRITE = 2, F_EOF = 4, F_ERR = 8 };

FILE _eigen_stdin  = { 0, F_READ,  0, 0, 0, EOF };
FILE _eigen_stdout = { 1, F_WRITE, 0, 0, 0, EOF };
FILE _eigen_stderr = { 2, F_WRITE, 0, 0, 0, EOF };

/* errno is per-thread now: __errno_location() (pthread.c) reads the TLS
   page; errno.h maps `errno` to it. No global storage here. */

/* Translate an eigen ABI return code (EIGEN_ERR_* negative values from
   kernel syscalls) into a POSIX errno, so POSIX-facing wrappers can set
   errno the way ported code expects. */
static int map_errno(int rc) {
    switch (rc) {
        case -2: return ENOENT;
        case -3: return ENOMEM;
        case -4: return EINVAL;
        case -5: return ESRCH;
        case -6: return EINVAL;
        default: return EIO;
    }
}

char* strerror(int e) {
    switch (e) {
        case EPERM:  return "Operation not permitted";
        case ENOENT: return "No such file or directory";
        case ESRCH:  return "No such process";
        case EINTR:  return "Interrupted system call";
        case EIO:    return "I/O error";
        case EBADF:  return "Bad file descriptor";
        case EAGAIN: return "Resource temporarily unavailable";
        case ENOMEM: return "Cannot allocate memory";
        case EACCES: return "Permission denied";
        case EEXIST: return "File exists";
        case ENOTDIR: return "Not a directory";
        case EISDIR: return "Is a directory";
        case EINVAL: return "Invalid argument";
        case ENFILE: return "Too many open files";
        case EFBIG:  return "File too large";
        case ENOSPC: return "No space left on device";
        case ERANGE: return "Numerical result out of range";
        case ENOSYS: return "Function not implemented";
        case ENAMETOOLONG: return "File name too long";
        case 0:      return "Success";
        default:     return "Unknown error";
    }
}

FILE* fopen(const char* path, const char* mode) {
    if (!path || !mode) { errno = EINVAL; return 0; }
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) { errno = ENOMEM; return 0; }
    f->fd = -1; f->flags = 0; f->data = 0; f->size = 0; f->pos = 0;
    f->unget = EOF;
    char m = mode[0];
    if (m == 'r') {
        if (eigen_fs_exists(path) <= 0) { free(f); errno = ENOENT; return 0; }
        long sz = eigen_fs_size(path);
        if (sz < 0) sz = 0;
        f->size = sz; f->flags = F_READ;
        if (sz > 0) {
            f->data = (unsigned char*)malloc((size_t)sz);
            if (!f->data) { free(f); errno = ENOMEM; return 0; }
            int n = eigen_fs_read_file(path, f->data, (int)sz);
            if (n < 0) { free(f->data); free(f); errno = map_errno(n); return 0; }
            f->size = n;
        }
        return f;
    }
    if (m == 'w' || m == 'a') {
        if (eigen_fs_exists(path) <= 0) {
            int rc = eigen_fs_create(path);
            if (rc != 0) { free(f); errno = map_errno(rc); return 0; }
        } else if (m == 'w') {
            if (eigen_fs_truncate(path) != 0) { free(f); errno = EIO; return 0; }
        }
        f->fd = eigen_open(path, 0);
        if (f->fd < 0) { free(f); errno = map_errno(f->fd); return 0; }
        _eigen_fd_register(f->fd, path);
        f->flags = F_WRITE;
        return f;
    }
    free(f);
    errno = EINVAL;
    return 0;
}

int fclose(FILE* f) {
    if (!f) return EOF;
    if (f->fd >= 0) { _eigen_fd_forget(f->fd); eigen_close(f->fd); }
    if (f->data) free(f->data);
    f->fd = -1; f->data = 0;
    return 0;
}

int feof(FILE* f)   { return f && (f->flags & F_EOF); }
int ferror(FILE* f) { return f && (f->flags & F_ERR); }
void clearerr(FILE* f) { if (f) f->flags &= ~(F_EOF | F_ERR); }
int fflush(FILE* f) { (void)f; return 0; }

int fgetc(FILE* f) {
    if (!f) return EOF;
    if (f->unget != EOF) { int c = f->unget; f->unget = EOF; return c; }
    if (f->flags & F_READ) {
        if (f->pos >= f->size) { f->flags |= F_EOF; return EOF; }
        return f->data[f->pos++];
    }
    f->flags |= F_ERR;
    return EOF;
}

int ungetc(int c, FILE* f) {
    if (!f || c == EOF) return EOF;
    f->unget = (unsigned char)c;
    f->flags &= ~F_EOF;
    return c;
}

int fileno(FILE* f) { return f ? f->fd : -1; }

int getc(FILE* f) { return fgetc(f); }
int putc(int c, FILE* f) { return fputc(c, f); }
int getchar(void) { return fgetc(&_eigen_stdin); }

FILE* freopen(const char* path, const char* mode, FILE* f) {
    if (!f) return 0;
    fclose(f);
    return fopen(path, mode);
}

FILE* popen(const char* cmd, const char* mode) {
    (void)cmd; (void)mode;
    errno = ENOSYS;
    return 0;
}

int pclose(FILE* f) {
    (void)f;
    errno = ENOSYS;
    return -1;
}

char* fgets(char* s, int n, FILE* f) {
    if (!s || n <= 0 || !f) return 0;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (!ptr || !f || !(f->flags & F_READ) || size == 0) return 0;
    long want = (long)(size * nmemb);
    long avail = f->size - f->pos;
    if (want > avail) want = avail;
    if (want > 0) { memcpy(ptr, f->data + f->pos, (size_t)want); f->pos += want; }
    if (f->pos >= f->size) f->flags |= F_EOF;
    return (size_t)(want / (long)size);
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (!ptr || !f || !(f->flags & F_WRITE)) { errno = EINVAL; return 0; }
    if (size == 0) return 0;
    long total = (long)(size * nmemb);
    if (total <= 0) return 0;
    int n = (int)eigen_write((uint32_t)f->fd, (const char*)ptr, (uint32_t)total);
    if (n < 0) { f->flags |= F_ERR; errno = map_errno(n); return 0; }
    return (size_t)(n / (long)size);
}

int fputc(int c, FILE* f) {
    char x = (char)c;
    return fwrite(&x, 1, 1, f) == 1 ? c : EOF;
}

int fputs(const char* s, FILE* f) {
    if (!s) return EOF;
    size_t len = strlen(s);
    return fwrite(s, 1, len, f) == len ? 0 : EOF;
}

int fseek(FILE* f, long off, int whence) {
    if (!f || !(f->flags & F_READ)) return -1;
    long base = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? f->pos : f->size;
    long np = base + off;
    if (np < 0) return -1;
    f->pos = np;
    f->flags &= ~F_EOF;
    return 0;
}

long ftell(FILE* f) { return f ? f->pos : -1L; }

int remove(const char* path) {
    int rc = eigen_fs_delete(path);
    if (rc != 0) errno = map_errno(rc);
    return rc == 0 ? 0 : -1;
}
int rename(const char* a, const char* b) {
    int rc = eigen_fs_rename(a, b);
    if (rc != 0) errno = map_errno(rc);
    return rc == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* POSIX-ish <unistd.h>: open/read/write/close/lseek/stat             */
/* ------------------------------------------------------------------ */
int open(const char* path, int flags, ...) {
    if (!path) { errno = EINVAL; return -1; }
    int fd = eigen_open(path, flags);
    if (fd < 0) { errno = map_errno(fd); return -1; }
    _eigen_fd_register(fd, path);
    return fd;
}

int pipe(int pipefd[2]) {
    int r = eigen_pipe(pipefd);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

ssize_t read(int fd, void* buf, size_t count) {
    int r = (int)eigen_read(fd, buf, (uint32_t)count);
    if (r < 0) { errno = map_errno(r); return -1; }
    return (ssize_t)r;
}

ssize_t write(int fd, const void* buf, size_t count) {
    int r = (int)eigen_write(fd, buf, (uint32_t)count);
    if (r < 0) { errno = map_errno(r); return -1; }
    return (ssize_t)r;
}

int close(int fd) {
    _eigen_fd_forget(fd);
    eigen_close(fd);
    return 0;
}

off_t lseek(int fd, off_t offset, int whence) {
    int r = eigen_lseek(fd, (int)offset, whence);
    if (r < 0) { errno = map_errno(r); return -1; }
    return (off_t)r;
}

int unlink(const char* path) {
    int r = eigen_fs_delete(path);
    if (r != 0) { errno = map_errno(r); return -1; }
    return 0;
}

int stat(const char* path, struct stat* st) {
    if (!path || !st) { errno = EINVAL; return -1; }
    struct eigen_stat es;
    int r = eigen_fs_stat(path, &es);
    if (r != 0) { errno = map_errno(r); return -1; }
    st->st_size = es.size;
    st->st_mode = (es.type == 1) ? S_IFDIR : S_IFREG;
    return 0;
}

void perror(const char* s) {
    if (s && s[0]) { fputs(s, stderr); fputs(": ", stderr); }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

int puts(const char* s) { fputs(s, stdout); fputc('\n', stdout); return 0; }
int putchar(int c) { return fputc(c, stdout); }

int sscanf(const char* str, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int done = 0;
    while (*fmt) {
        if (*fmt != '%') { fmt++; continue; }
        fmt++;
        if (*fmt == 'x' || *fmt == 'X' || *fmt == 'd' || *fmt == 'i' || *fmt == 'u') {
            while (*str == ' ' || *str == '\t') str++;
            int neg = 0; long v = 0;
            if (*str == '-') { neg = 1; str++; } else if (*str == '+') str++;
            if (*fmt == 'x' || *fmt == 'X') {
                while ((*str>='0'&&*str<='9')||(*str>='a'&&*str<='f')||(*str>='A'&&*str<='F')) {
                    int c = *str | 0x20;
                    int d = (*str>='0'&&*str<='9') ? *str-'0' : (c>='a'&&c<='f') ? c-'a'+10 : 0;
                    v = v*16 + d; str++;
                }
            } else { while (*str>='0'&&*str<='9') { v=v*10+(*str-'0'); str++; } }
            v = neg ? -v : v;
            if (*fmt=='d'||*fmt=='i'||*fmt=='x'||*fmt=='X') { int* p=va_arg(ap,int*); *p=(int)v; }
            else { unsigned* p=va_arg(ap,unsigned*); *p=(unsigned)v; }
            done++;
        } else if (*fmt == 's') {
            char* p = va_arg(ap, char*);
            while (*str==' '||*str=='\t') str++;
            while (*str && *str!=' ' && *str!='\t') *p++ = *str++;
            *p = 0; done++;
        } else if (*fmt == 'c') {
            char* p = va_arg(ap, char*); *p = *str++; done++;
        } else { fmt++; continue; }
        fmt++;
    }
    va_end(ap);
    return done;
}

/* 10^n as a double, n in [-308, 308]. */
static double pow10(int n) {
    double r = 1.0;
    if (n >= 0) { while (n-- > 0) r *= 10.0; }
    else { double d = 1.0; while (n++ < 0) d *= 10.0; r = 1.0 / d; }
    return r;
}

/* Emit v in the given conversion ('f'/'e'/'E'/'g'/'G') with prec digits,
   honouring the alternate flag; includes the leading '-'.
   Digits are extracted by repeated multiply (works for any precision,
   e.g. awk's "%.30g" for integral values), then the (prec+1)-th digit
   rounds half away from zero. */
static int format_double(char* out, int cap, double v, int conv, int prec,
                         int alt) {
    (void)cap;
    char* o = out;
    int neg = (v < 0 || (v == 0.0 && 1.0 / v < 0));
    if (neg) v = -v;
    int e10 = 0;
    if (v > 0) {
        double t = v;
        while (t >= 10) { t /= 10; if (++e10 > 300) break; }
        while (t < 1)   { t *= 10; if (--e10 < -300) break; }
    }
    int use_e = (conv == 'e' || conv == 'E');
    if (conv == 'g' || conv == 'G') {
        if (prec == 0) prec = 1;
        use_e = (e10 < -4 || e10 >= prec);
        prec--;   /* %g precision = significant digits - 1 */
    }
    if (neg) *o++ = '-';

    /* significant digits to extract (the extra one drives rounding) */
    int ndig;
    if (use_e || conv == 'g' || conv == 'G') ndig = prec + 1;
    else ndig = prec + e10 + 1;
    if (ndig < 0) ndig = 0;
    if (ndig > 30) ndig = 30;

    char dc[32];
    int dn = 0;
    if (v == 0) {
        dc[dn++] = '0';
        e10 = 0;
    } else if (v <= 9.2e18 && v == (double)(long long)v) {
        /* integral value: digits are exact, no rounding noise */
        unsigned long long u = (unsigned long long)(long long)v;
        char tmp[24];
        int tn = 0;
        do { tmp[tn++] = (char)('0' + u % 10); u /= 10; } while (u > 0);
        while (tn > 0) dc[dn++] = tmp[--tn];
        while (dn < ndig + 1) dc[dn++] = '0';
    } else {
        double m = v / pow10(e10);          /* m in [1, 10) */
        double rem = m;
        for (int i = 0; i < ndig + 1; i++) {
            int d = (int)rem;
            if (d > 9) d = 9;
            dc[dn++] = (char)('0' + d);
            rem = (rem - (double)d) * 10.0;
        }
    }
    dn--;                               /* drop the rounding digit */
    if (dc[dn] - '0' >= 5) {            /* round half away from zero */
        int i = dn - 1;
        while (i >= 0 && dc[i] == '9') { dc[i] = '0'; i--; }
        if (i >= 0) { dc[i]++; }
        else {
            memmove(&dc[1], &dc[0], (size_t)dn);
            dc[0] = '1';
            dn++;
            e10++;
        }
    }

    if (use_e) {
        *o++ = dc[0];
        if (prec > 0) {
            *o++ = '.';
            for (int i = 1; i < dn; i++) *o++ = dc[i];
            for (int i = dn; i <= prec; i++) *o++ = '0';
        }
        if ((conv == 'g' || conv == 'G') && !alt) {
            while (o[-1] == '0') o--;
            if (o[-1] == '.') o--;
        }
        *o++ = (conv == 'E') ? 'E' : 'e';
        int ae = (e10 < 0) ? -e10 : e10;
        *o++ = (e10 < 0) ? '-' : '+';
        if (ae < 10) *o++ = '0';
        char es[8]; int en = 0;
        do { es[en++] = (char)('0' + ae % 10); ae /= 10; } while (ae > 0);
        while (en > 0) *o++ = es[--en];
    } else {
        int intdig = e10 + 1;
        if (intdig < 0) intdig = 0;
        if (intdig > dn) intdig = dn;
        for (int i = 0; i < intdig; i++) *o++ = dc[i];
        if (intdig == 0) *o++ = '0';
        if (prec > 0) {
            *o++ = '.';
            int frac = dn - intdig;
            int zeros = (e10 < 0) ? (-e10 - 1) : 0;
            if (zeros > prec) zeros = prec;
            for (int i = 0; i < zeros; i++) *o++ = '0';
            for (int i = intdig; i < dn; i++) *o++ = dc[i];
            int emitted = zeros + frac;
            for (int i = emitted; i < prec; i++) *o++ = '0';
            if (conv == 'g' || conv == 'G') {
                while (o[-1] == '0') o--;
                if (o[-1] == '.') o--;
            }
        }
    }
    *o = 0;
    return (int)(o - out);
}

/* Full-featured vsnprintf for the engine: flags (-, 0, +, space, #),
   width, precision (%.3d, %02i, %2.2d, %.8s, %0.2f), length mods (l/ll/z/h)
   and the d i u x X o c s p f % conversions. DOOM builds lump names with
   "%\\.3d" (HU font: STCFN033..) so precision support is mandatory. */
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    size_t len = 0;
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { if (buf && len + 1 < size) buf[len] = *p; len++; continue; }
        p++;
        if (*p == 0) break;
        int left = 0, zero = 0, plus = 0, sp = 0, alt = 0;
        for (;; p++) {
            if      (*p == '-') left = 1;
            else if (*p == '0') zero = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') sp = 1;
            else if (*p == '#') alt = 1;
            else break;
        }
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        int prec = -1;
        if (*p == '.') { p++; prec = 0; while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; } }
        int llen = 0;
        while (*p == 'l' || *p == 'z' || *p == 'h' || *p == 'j' || *p == 't' || *p == 'q') { if (*p == 'l' || *p == 'j' || *p == 'q') llen++; p++; }
        char conv = *p;
        if (conv == 0) break;
        char t[96]; int tn = 0;
        if (conv == '%') {
            t[tn++] = '%';
        } else if (conv == 'c') {
            t[tn++] = (char)va_arg(ap, int);
        } else if (conv == 's') {
            const char* s = va_arg(ap, const char*);
            int sl = 0; while (s[sl] && (prec < 0 || sl < prec)) sl++;
            int pad = width > sl ? width - sl : 0;
            for (int k = 0; k < pad && !left; k++) t[tn++] = ' ';
            for (int k = 0; k < sl; k++) t[tn++] = s[k];
            for (int k = 0; k < pad && left; k++) t[tn++] = ' ';
        } else if (conv == 'd' || conv == 'i') {
            long v = (llen >= 2) ? (long)va_arg(ap, long long) : (llen == 1) ? (long)va_arg(ap, long) : (long)va_arg(ap, int);
            int neg = 0; unsigned long u;
            if (v < 0) { neg = 1; u = (unsigned long)(-v); } else u = (unsigned long)v;
            char dt[32]; int dn = 0;
            if (u == 0 && prec != 0) dt[dn++] = '0';
            while (u > 0) { dt[dn++] = (char)('0' + u % 10); u /= 10; }
            while (dn < prec) dt[dn++] = '0';
            int total = dn + neg + (plus && !neg ? 1 : 0);
            int pad = width > total ? width - total : 0;
            for (int k = 0; k < pad && !left && !zero; k++) t[tn++] = ' ';
            if (neg) t[tn++] = '-';
            else if (plus) t[tn++] = '+';
            else if (sp && pad == 0) t[tn++] = ' ';
            for (int k = 0; k < pad && !left && zero; k++) t[tn++] = '0';
            while (dn > 0) t[tn++] = dt[--dn];
            for (int k = 0; k < pad && left; k++) t[tn++] = ' ';
        } else if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            unsigned long u = (llen >= 2) ? (unsigned long)va_arg(ap, unsigned long long) : (llen == 1) ? (unsigned long)va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            const char* dc = (conv == 'X') ? "0123456789ABCDEF"
                          : (conv == 'o') ? "01234567"
                          : "0123456789abcdef";
            int base = (conv == 'o') ? 8 : 16;
            char dt[32]; int dn = 0;
            if (u == 0 && prec != 0) dt[dn++] = '0';
            while (u > 0) { dt[dn++] = dc[u % base]; u /= base; }
            while (dn < prec) dt[dn++] = '0';
            int pad = width > dn ? width - dn : 0;
            for (int k = 0; k < pad && !left && !zero; k++) t[tn++] = ' ';
            if (alt && (conv == 'x' || conv == 'X') && dn > 0 && dt[dn-1] != '0') {
                t[tn++] = '0'; t[tn++] = (conv == 'X') ? 'X' : 'x';
            }
            for (int k = 0; k < pad && !left && zero; k++) t[tn++] = '0';
            while (dn > 0) t[tn++] = dt[--dn];
            for (int k = 0; k < pad && left; k++) t[tn++] = ' ';
        } else if (conv == 'p') {
            unsigned long v = (unsigned long)va_arg(ap, void*);
            char dt[32]; int dn = 0;
            if (v == 0) dt[dn++] = '0';
            while (v > 0) { dt[dn++] = "0123456789abcdef"[v % 16]; v /= 16; }
            t[tn++] = '0'; t[tn++] = 'x';
            while (dn > 0) t[tn++] = dt[--dn];
        } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E'
                   || conv == 'g' || conv == 'G') {
            double v = va_arg(ap, double);
            char fb[160];
            int fl = format_double(fb, sizeof fb, v, conv,
                                   (prec < 0) ? 6 : prec, alt);
            int neg = (fb[0] == '-');
            int pad = width > fl ? width - fl : 0;
            int fi = 0;
            for (int k = 0; k < pad && !left && !zero; k++) t[tn++] = ' ';
            if (neg && zero) { t[tn++] = '-'; fi = 1; }
            for (int k = 0; k < pad && !left && zero; k++) t[tn++] = '0';
            for (; fi < fl; fi++) t[tn++] = fb[fi];
            for (int k = 0; k < pad && left; k++) t[tn++] = ' ';
        } else {
            t[tn++] = '?';
        }
        for (int k = 0; k < tn; k++) { if (buf && len + 1 < size) buf[len] = t[k]; len++; }
    }
    if (buf && len < size) buf[len] = 0;
    return (int)len;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) { va_list ap; va_start(ap,fmt); int r=vsnprintf(buf,size,fmt,ap); va_end(ap); return r; }
int sprintf(char* buf, const char* fmt, ...) { va_list ap; va_start(ap,fmt); int r=vsnprintf(buf,4096,fmt,ap); va_end(ap); return r; }
int vprintf(const char* fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int printf(const char* fmt, ...) { va_list ap; va_start(ap,fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r; }
int vfprintf(FILE* f, const char* fmt, va_list ap) {
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    if (n < 0) return n;
    if ((size_t)n >= sizeof tmp) n = (int)(sizeof tmp - 1);   // clamp (total-length semantics)
    if (f) fwrite(tmp, 1, (size_t)n, f);
    return n;
}
int fprintf(FILE* f, const char* fmt, ...) { va_list ap; va_start(ap,fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r; }

/* atof for m_config float parsing. */
/* strtod: [+-]digits[.digits][e[+-]digits]; sets *end past the match. */
double strtod(const char* s, char** end) {
    const char* start = s;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    double v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (*s == '.') {
        s++;
        double f = 0.1;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f *= 0.1; s++; any = 1; }
    }
    int e10 = 0;
    if (any && (*s == 'e' || *s == 'E')) {
        const char* es = s;
        s++;
        int en = 0;
        if (*s == '+' || *s == '-') { en = (*s == '-'); s++; }
        int ev = 0, edig = 0;
        while (*s >= '0' && *s <= '9') { ev = ev * 10 + (*s - '0'); s++; edig = 1; }
        if (!edig) { s = es; }
        else if (en) e10 = -ev;
        else e10 = ev;
    }
    if (!any) {
        if (end) *end = (char*)start;
        return 0;
    }
    while (e10 > 0) { v *= 10; e10--; }
    while (e10 < 0) { v /= 10; e10++; }
    if (end) *end = (char*)s;
    return neg ? -v : v;
}

double atof(const char* s) {
    double v = 0, frac = 0, scale = 1; int neg = 0;
    while (*s==' '||*s=='\t') s++;
    if (*s=='-'){neg=1;s++;} else if (*s=='+') s++;
    while (*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); s++; }
    if (*s=='.'){ s++; while(*s>='0'&&*s<='9'){ frac=frac*10+(*s-'0'); scale*=10; s++; } v+=frac/scale; }
    return neg?-v:v;
}

/* fscanf stub — nothing in EigenOS feeds scanf-style input; callers that
 * parse optional system files (fontconfig) just get EOF. */
#include <stdarg.h>
int fscanf(void *f, const char *fmt, ...) { (void)f; (void)fmt; return -1; }
