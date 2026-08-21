/* Minimal freestanding stdlib.h for the DOOM port. */
#ifndef EIGEN_SHIM_STDLIB_H
#define EIGEN_SHIM_STDLIB_H
#include <stddef.h>
#include <wctype.h>

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void  free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
int   atoi(const char* s);
long  atol(const char* s);
long  strtol(const char* s, char** end, int base);
double atof(const char* s);
double strtod(const char* s, char** end);
void  exit(int code);
void  abort(void);
char* getenv(const char* name);
int   system(const char* cmd);
void  qsort(void* base, size_t nmemb, size_t size,
            int (*cmp)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*));
int   abs(int x);
long  labs(long x);
void  srand(unsigned seed);
int   rand(void);
void  srandom(unsigned long seed);
long  random(void);
char* realpath(const char* path, char* resolved);
#define RAND_MAX 0x7FFFFFFF
#define MB_CUR_MAX 1
int   mbtowc(wchar_t* pwc, const char* s, size_t n);
int   wctomb(char* s, wchar_t wc);

#ifdef __cplusplus
}
#endif

#endif
