/* Minimal POSIX-ish <unistd.h> for ring-3 user apps.
   Provides open/read/write/close/lseek/stat over the eigen syscalls.
   Writes always append to the file end (the flat kernel FS has no holes);
   lseek only affects the read position. */
#ifndef EIGEN_SHIM_UNISTD_H
#define EIGEN_SHIM_UNISTD_H

#include <stddef.h>
#include <sys/stat.h>   /* struct stat, stat(), S_IS* */
#include <sys/types.h>  /* pid_t */

typedef long ssize_t;
typedef long off_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Open flags: accepted for compatibility, mostly ignored (no real modes). */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400
#define O_NONBLOCK 0x800

/* access() mode bits */
#define F_OK 0
#define X_OK 0x01
#define W_OK 0x02
#define R_OK 0x04

int      open(const char* path, int flags, ...);
ssize_t  read(int fd, void* buf, size_t count);
ssize_t  write(int fd, const void* buf, size_t count);
int      close(int fd);
off_t    lseek(int fd, off_t offset, int whence);
int      unlink(const char* path);
int      pipe(int pipefd[2]);
pid_t    getpid(void);
pid_t    getppid(void);
int      isatty(int fd);
int      access(const char* path, int mode);
char*    getcwd(char* buf, size_t size);
int      chdir(const char* path);
int      ftruncate(int fd, off_t length);

#endif