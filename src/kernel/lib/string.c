/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include <stddef.h>
#include <stdint.h>

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        uint8_t* d_end = d + n;
        const uint8_t* s_end = s + n;
        while (n--) {
            *--d_end = *--s_end;
        }
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    if (c == 0) return (char*)s;
    return 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}

char* strstr(const char* h, const char* n) {
    if (!*n) return (char*)h;
    while (*h) {
        const char *p = h, *q = n;
        while (*q && *p == *q) { p++; q++; }
        if (!*q) return (char*)h;
        h++;
    }
    return 0;
}

char* strncpy(char* dest, const char* src, int n) {
    int i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

/* The following string routines are NOT provided by src/commands/commands.c,
   so they are added here for completeness of the kernel libc.  strlen,
   strcpy, strcmp and strncmp already exist in commands.c and must not be
   redefined here (doing so produces duplicate symbols at link time). */

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while (*src) *d++ = *src++;
    *d = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, int n) {
    char* d = dest;
    while (*d) d++;
    int i = 0;
    while (i < n && src[i]) {
        *d++ = src[i++];
    }
    *d = '\0';
    return dest;
}

static int klib_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        int c1 = klib_tolower((unsigned char)*s1);
        int c2 = klib_tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int strncasecmp(const char* s1, const char* s2, int n) {
    int i = 0;
    while (i < n && *s1 && *s2) {
        int c1 = klib_tolower((unsigned char)*s1);
        int c2 = klib_tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++; i++;
    }
    if (i == n) return 0;
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}
