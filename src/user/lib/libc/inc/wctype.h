/* Minimal wctype.h for awk: wchar/wint types + byte-wide mbtowc/wctomb. */
#ifndef EIGEN_SHIM_WCTYPE_H
#define EIGEN_SHIM_WCTYPE_H
#include <stddef.h>

#ifndef __cplusplus
typedef int wchar_t;
#endif
typedef unsigned int wint_t;
#define WEOF ((wint_t)-1)

int iswspace(wint_t wc);
int iswdigit(wint_t wc);
int iswalpha(wint_t wc);
int iswalnum(wint_t wc);
wint_t towupper(wint_t wc);
wint_t towlower(wint_t wc);

#endif