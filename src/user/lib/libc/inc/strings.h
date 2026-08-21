/* Minimal freestanding strings.h (BSD case-insensitive funcs). */
#ifndef EIGEN_SHIM_STRINGS_H
#define EIGEN_SHIM_STRINGS_H
#include <stddef.h>
int  strcasecmp(const char* a, const char* b);
int  strncasecmp(const char* a, const char* b, size_t n);
void bzero(void* s, size_t n);
char* index(const char* s, int c);
char* rindex(const char* s, int c);
#endif
