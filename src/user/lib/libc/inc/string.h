/* Minimal freestanding string.h for the DOOM port. */
#ifndef EIGEN_SHIM_STRING_H
#define EIGEN_SHIM_STRING_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
void* memchr(const void* s, int c, size_t n);
size_t strlen(const char* s);
int   strcmp(const char* a, const char* b);
int   strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t n);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, size_t n);
 char* strchr(const char* s, int c);
 char* strrchr(const char* s, int c);
 char* strpbrk(const char* s, const char* accept);
 char* strstr(const char* hay, const char* needle);
char* strdup(const char* s);
char* strtok(char* s, const char* delim);
int   strcasecmp(const char* a, const char* b);
int   strncasecmp(const char* a, const char* b, size_t n);
char* strerror(int errnum);
double atof(const char* s);
long   strtol(const char* s, char** end, int base);
unsigned long strtoul(const char* s, char** end, int base);

#ifdef __cplusplus
}
#endif

#endif
