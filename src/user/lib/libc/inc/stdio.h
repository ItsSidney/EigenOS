/* Freestanding stdio.h for ring-3 user apps.
   Read streams buffer the whole file in memory (the kernel FS has no
   per-fd offsets); write streams write through to the fd. stdout/stderr
   route to the kernel log. */
#ifndef EIGEN_SHIM_STDIO_H
#define EIGEN_SHIM_STDIO_H
#include <stdarg.h>
#include <stddef.h>

typedef struct _eigen_FILE FILE;
extern FILE _eigen_stdin;
extern FILE _eigen_stdout;
extern FILE _eigen_stderr;
#define stdin  (&_eigen_stdin)
#define stdout (&_eigen_stdout)
#define stderr (&_eigen_stderr)

int  fscanf(void *f, const char* fmt, ...);
int  printf(const char* fmt, ...);
int  fprintf(FILE* stream, const char* fmt, ...);
int  vprintf(const char* fmt, va_list ap);
int  vfprintf(FILE* stream, const char* fmt, va_list ap);
int  vasprintf(char** strp, const char* fmt, va_list ap);
int  sprintf(char* buf, const char* fmt, ...);
int  snprintf(char* buf, size_t size, const char* fmt, ...);
int  vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int  puts(const char* s);
int  putchar(int c);
int  fputc(int c, FILE* stream);
int  fputs(const char* s, FILE* stream);
int  fflush(FILE* stream);
int  fclose(FILE* stream);
FILE* fopen(const char* path, const char* mode);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int  fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
int  fgetc(FILE* stream);
int  ungetc(int c, FILE* stream);
int  fileno(FILE* stream);
FILE* freopen(const char* path, const char* mode, FILE* stream);
FILE* popen(const char* cmd, const char* mode);
int   pclose(FILE* stream);
char* fgets(char* s, int n, FILE* stream);
int  feof(FILE* stream);
int  ferror(FILE* stream);
void clearerr(FILE* stream);
int  remove(const char* path);
int  rename(const char* oldp, const char* newp);
void perror(const char* s);
int  sscanf(const char* str, const char* fmt, ...);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
#define FOPEN_MAX 20
#define BUFSIZ 512
int  getc(FILE* stream);
int  putc(int c, FILE* stream);
int  getchar(void);

#endif