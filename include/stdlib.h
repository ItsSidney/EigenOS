/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void* malloc(size_t size);
void* calloc(size_t n, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);
int rand(void);
void srand(unsigned int seed);
int abs(int x);
long labs(long x);
int atoi(const char* s);
double atof(const char* s);
void exit(int code);

/* used by the kernel-side FreeType port */
typedef int (*qsort_cmp)(const void*, const void*);
void qsort(void* base, size_t nmemb, size_t size, qsort_cmp cmp);
long strtol(const char* s, char** endp, int base);
char* getenv(const char* name);

#endif
