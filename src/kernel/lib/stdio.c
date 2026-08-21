/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/lib/stdio.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <kernel/log.h>

static void reverse(char* s, int len) {
    int i, j;
    char t;
    for (i = 0, j = len - 1; i < j; i++, j--) {
        t = s[i]; s[i] = s[j]; s[j] = t;
    }
}

static int utoa(unsigned long n, char* buf, int base, int width, int zero) {
    int i = 0;
    const char* digits = "0123456789ABCDEF";
    if (n == 0) buf[i++] = '0';
    while (n > 0) {
        buf[i++] = digits[n % base];
        n /= base;
    }
    while (i < width) buf[i++] = zero ? '0' : ' ';
    reverse(buf, i);
    return i;
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    int pos = 0;

    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            if ((size_t)pos + 1 < size) buf[pos] = *p;
            pos++;
            continue;
        }
        p++;
        int width = 0, zero = 0;
        while (*p >= '0' && *p <= '9') {
            if (*p == '0' && width == 0) zero = 1;
            width = width * 10 + (*p - '0');
            p++;
        }
        char tmp[64];
        int len = 0, i;
        switch (*p) {
            case 'd': {
                int v = va_arg(ap, int);
                if (v < 0) {
                    if ((size_t)pos + 1 < size) buf[pos] = '-';
                    pos++;
                    v = -v;
                }
                len = utoa((unsigned long)v, tmp, 10, width, zero);
                for (i = 0; i < len; i++) {
                    if ((size_t)pos + 1 < size) buf[pos] = tmp[i];
                    pos++;
                }
                break;
            }
            case 'u':
                len = utoa(va_arg(ap, unsigned int), tmp, 10, width, zero);
                for (i = 0; i < len; i++) {
                    if ((size_t)pos + 1 < size) buf[pos] = tmp[i];
                    pos++;
                }
                break;
            case 'x':
                len = utoa(va_arg(ap, unsigned int), tmp, 16, width, zero);
                for (i = 0; i < len; i++) {
                    if ((size_t)pos + 1 < size) buf[pos] = tmp[i];
                    pos++;
                }
                break;
            case 'X': {
                len = utoa(va_arg(ap, unsigned int), tmp, 16, width, zero);
                for (i = 0; i < len; i++) {
                    char c = tmp[i];
                    if (c >= 'a' && c <= 'f') c = c - 'a' + 'A';
                    if ((size_t)pos + 1 < size) buf[pos] = c;
                    pos++;
                }
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) {
                    if ((size_t)pos + 1 < size) buf[pos] = *s;
                    pos++;
                    s++;
                }
                break;
            }
            case 'p': {
                void* vp = va_arg(ap, void*);
                unsigned long addr = (unsigned long)vp;
                const char* hex = "0123456789abcdef";
                char abuf[20];
                int ai = 0;
                if (addr == 0) { abuf[ai++] = '0'; }
                while (addr > 0) { abuf[ai++] = hex[addr % 16]; addr /= 16; }
                reverse(abuf, ai);
                if ((size_t)pos + 1 < size) buf[pos] = '0'; pos++;
                if ((size_t)pos + 1 < size) buf[pos] = 'x'; pos++;
                for (i = 0; i < ai; i++) {
                    if ((size_t)pos + 1 < size) buf[pos] = abuf[i];
                    pos++;
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                if ((size_t)pos + 1 < size) buf[pos] = c;
                pos++;
                break;
            }
            default:
                if ((size_t)pos + 1 < size) buf[pos] = *p;
                pos++;
                break;
        }
    }

    if (size > 0 && (size_t)pos < size) buf[pos] = 0;
    else if (size > 0) buf[size - 1] = 0;
    return pos;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

double atof(const char* s) {
    double res = 0.0, div = 1.0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { res = res * 10.0 + (*s++ - '0'); }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { res = res * 10.0 + (*s++ - '0'); div *= 10.0; }
    }
    return sign * res / div;
}

void itoa(uint64_t n, char* s) {
    int i = 0;
    do {
        s[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    s[i] = 0;
    int len = i;
    for (i = 0; i < len / 2; i++) {
        char t = s[i]; s[i] = s[len - 1 - i]; s[len - 1 - i] = t;
    }
}

struct __FILE {
    int slot;
};

static struct __FILE _stdin  = { 0 };
static struct __FILE _stdout = { 1 };
static struct __FILE _stderr = { 2 };
FILE* stdin  = &_stdin;
FILE* stdout = &_stdout;
FILE* stderr = &_stderr;

static void kprint_str(const char* s) {
    if (!s) return;
    char buf[256];
    size_t i = 0;
    while (s[i] && i < sizeof(buf) - 1) {
        if (s[i] == '\n' && i > 0 && buf[i-1] != '\r') {
            buf[i++] = '\r';
        }
        buf[i++] = s[i];
    }
    buf[i] = 0;
    klog(buf);
}

int vprintf(const char* fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    kprint_str(buf);
    return n;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE* f, const char* fmt, va_list ap) {
    (void)f;
    return vprintf(fmt, ap);
}

int fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char* buf, const char* fmt, va_list ap) {
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char* s) {
    kprint_str(s);
    klog("\n");
    return 0;
}
