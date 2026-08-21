/* Minimal locale.h: the OS is a C locale only. */
#ifndef EIGEN_SHIM_LOCALE_H
#define EIGEN_SHIM_LOCALE_H

#define LC_ALL      0
#define LC_CTYPE    1
#define LC_NUMERIC  2
#define LC_TIME     3
#define LC_COLLATE  4
#define LC_MONETARY 5
#define LC_MESSAGES 6

typedef void* locale_t;  /* EigenOS: opaque locale handle for portability */

char* setlocale(int category, const char* locale);

#endif