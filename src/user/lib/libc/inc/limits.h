/* Minimal limits.h stub for the Eina freestanding port. */
#ifndef EIGEN_SHIM_LIMITS_H
#define EIGEN_SHIM_LIMITS_H

#define PATH_MAX   4096
#define NAME_MAX   255
#define FILENAME_MAX 4096
#define PIPE_BUF   512

/* Standard integer limits (mirrors <stdint.h> constants from the compiler). */
#define INT_MAX    2147483647
#define INT_MIN    (-2147483647 - 1)
#define UINT_MAX   4294967295U
#define LONG_MAX   9223372036854775807L
#define LONG_MIN   (-9223372036854775807L - 1)
#define ULONG_MAX  18446744073709551615UL
#define LLONG_MAX  9223372036854775807LL
#define LLONG_MIN  (-9223372036854775807LL - 1)
#define ULLONG_MAX 18446744073709551615ULL
#define CHAR_BIT   8
#define SCHAR_MAX  127
#define SCHAR_MIN  (-128)
#define UCHAR_MAX  255
#define SHRT_MAX   32767
#define SHRT_MIN   (-32768)
#define USHRT_MAX  65535

#endif
