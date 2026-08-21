/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)

typedef struct __FILE FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int vfprintf(FILE* f, const char* fmt, va_list ap);
int vsprintf(char* buf, const char* fmt, va_list ap);
int puts(const char* s);

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* f);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
int fseek(FILE* f, long offset, int whence);
long ftell(FILE* f);
int feof(FILE* f);
int fflush(FILE* f);
int remove(const char* path);
int rename(const char* old, const char* new_);
int sscanf(const char* s, const char* fmt, ...);

#endif
