/* Minimal fnmatch.c — bundled for the Eina freestanding port.
 * Supports *, ?, [...] and backslash escaping. Used by eina_fnmatch. */
#include <stddef.h>
#include "fnmatch.h"

static int fnmatch_charset(const char** p, int c) {
    const char* pat = *p;
    int neg = 0, match = 0, was_dash = 0, prev = 0;
    if (*pat == '!' || *pat == '^') { neg = 1; pat++; }
    while (*pat && *pat != ']') {
        if (*pat == '-' && prev && pat[1] != ']') {
            int lo = prev, hi = pat[1];
            if (c >= lo && c <= hi) match = 1;
            pat += 2;
            was_dash = 1;
            continue;
        }
        if (*pat == c) match = 1;
        prev = (unsigned char)*pat;
        pat++;
        was_dash = 0;
    }
    if (*pat == ']') pat++;
    *p = pat;
    return neg ? !match : match;
}

static int do_fnmatch(const char* pat, const char* str, int flags) {
    int escape = !(flags & FNM_NOESCAPE);
    while (*pat) {
        switch (*pat) {
        case '*':
            while (pat[1] == '*') pat++;
            if (!pat[1]) return 0;
            for (; *str; str++) {
                if (do_fnmatch(pat + 1, str, flags) == 0) return 0;
            }
            return 1;
        case '?':
            if (!*str) return 1;
            str++; pat++;
            break;
        case '[': {
            const char* p = pat + 1;
            if (escape && p[0] == '\\') p++;
            if (!*str) return 1;
            if (!fnmatch_charset(&p, (unsigned char)*str)) return 1;
            str++; pat = p;
            break;
        }
        case '\\':
            if (escape && pat[1]) {
                if (*str != pat[1]) return 1;
                str++; pat += 2;
                break;
            }
            /* fall through */
        default:
            if (*pat != *str) return 1;
            str++; pat++;
            break;
        }
    }
    return (*str != '\0');
}

int __fnmatch(const char* pattern, const char* string, int flags) {
    return do_fnmatch(pattern, string, flags) ? FNM_NOMATCH : 0;
}

int fnmatch(const char* pattern, const char* string, int flags) {
    return __fnmatch(pattern, string, flags);
}
