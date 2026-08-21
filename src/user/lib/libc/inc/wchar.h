/* Freestanding wchar.h shim for EigenOS ring-3 / EFL port.
 * EINA_SIZEOF_WCHAR_T=1 makes Eina prefer uint32_t, but some TUs
 * still pull this header transitively. We provide just the types
 * needed — nothing that drags in host FILE or bits/. */
#ifndef EIGEN_SHIM_WCHAR_H
#define EIGEN_SHIM_WCHAR_H
#include <stddef.h>
#include <stdint.h>

#ifndef __cplusplus
# ifndef _WCHAR_T_DEFINED_
#  define _WCHAR_T_DEFINED_
typedef int wchar_t;
# endif
#endif
typedef unsigned int wint_t;
typedef struct { int __state; } mbstate_t;

#define WEOF        ((wint_t)-1)
#define WCHAR_MIN   (-2147483648)
#define WCHAR_MAX   (2147483647)

/* Subset Eina actually calls: */
size_t   wcslen(const wchar_t *s);
int      wcscmp(const wchar_t *a, const wchar_t *b);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
int      wctomb(char *s, wchar_t wc);
int      mbtowc(wchar_t *pwc, const char *s, size_t n);
size_t   mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t   wcstombs(char *dst, const wchar_t *src, size_t n);

#endif /* EIGEN_SHIM_WCHAR_H */
