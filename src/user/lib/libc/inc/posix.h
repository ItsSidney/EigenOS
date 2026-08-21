#ifndef EIGEN_SHIM_POSIX_H
#define EIGEN_SHIM_POSIX_H

/* posix.h — POSIX surface for ported libraries (EFL/FreeType).
 *
 * Declares the functions libc/posix.c implements: mmap, environment,
 * time, directories and dynamic-loading stubs. Portable C libraries
 * expect these headers to exist even when the underlying feature is
 * emulated in ring 3. */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- memory mapping (emulated: allocate + read the whole file) ---- */
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANON      0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FAILED    ((void*)-1)
void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off);
int   munmap(void* addr, size_t len);
int   mprotect(void* addr, size_t len, int prot);

/* ---- environment ---- */
char* getenv(const char* name);
int   setenv(const char* name, const char* value, int overwrite);
int   unsetenv(const char* name);
int   putenv(char* string);
extern char** environ;

/* ---- time ---- (struct tm + decls live in time.h to match <time.h>) ---- */
struct tm* localtime(const time_t* t);
struct tm* gmtime(const time_t* t);
time_t mktime(struct tm* tm);
char* asctime(const struct tm* tm);
char* ctime(const time_t* t);
size_t strftime(char* s, size_t max, const char* fmt, const struct tm* tm);

/* ---- sleep / process ---- */
int nanosleep(const struct timespec* req, struct timespec* rem);
int usleep(unsigned long usec);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
uid_t getgid(void);
char* getcwd(char* buf, size_t size);

/* ---- directories ---- */
typedef struct DIR DIR;
struct dirent {
    long d_ino;
    char d_name[256];
};
DIR* opendir(const char* path);
struct dirent* readdir(DIR* d);
int closedir(DIR* d);

/* ---- dynamic linking (stubs: everything is statically linked) ---- */
void* dlopen(const char* filename, int flags);
void* dlsym(void* handle, const char* symbol);
int   dlclose(void* handle);
char* dlerror(void);
#define RTLD_LAZY 0x1
#define RTLD_NOW  0x2

/* ---- misc string helpers ---- */
void* memchr(const void* s, int c, size_t n);
char* strtok_r(char* s, const char* delim, char** save);
size_t strnlen(const char* s, size_t maxlen);
char* strndup(const char* s, size_t n);

/* ---- internal: fd->path registry (libc.c + posix.c cooperate) ---- */
int _eigen_fd_register(int fd, const char* path);
void _eigen_fd_forget(int fd);
const char* _eigen_fd_path(int fd);
int map_posix_errno(int rc);

#ifdef __cplusplus
}
#endif

#endif /* EIGEN_SHIM_POSIX_H */