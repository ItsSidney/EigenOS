/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _STRING_H
#define _STRING_H

#include "kernel/lib/string.h"

/* Also include other string functions defined elsewhere in Eigen */
#include <stdint.h>

int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, int n);
int strlen(const char* s);
void strcpy(char* dest, const char* src);
int memcmp(const void* s1, const void* s2, size_t n);
void itoa(uint64_t n, char* s);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strdup(const char* s);
char* strstr(const char* h, const char* n);
char* strncpy(char* dest, const char* src, int n);
char* strcat(char* dest, const char* src);
void* memchr(const void* s, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);

#endif
