/* Minimal freestanding inttypes.h for the DOOM port. */
#ifndef EIGEN_SHIM_INTTYPES_H
#define EIGEN_SHIM_INTTYPES_H
#include <stdint.h>
#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  "lld"
#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "i"
#define PRIi64  "lli"
#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  "llu"
#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  "llx"
#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  "X"
#define PRIX64  "llX"

#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  "lld"
#define SCNi8   "hhi"
#define SCNi16  "hi"
#define SCNi32  "i"
#define SCNi64  "lli"
#define SCNo8   "hho"
#define SCNo16  "ho"
#define SCNo32  "o"
#define SCNo64  "llo"
#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  "llu"
#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  "llx"

/* Pointer-format macros (used by Eina safepointer / logging). */
#ifndef __WORDSIZE
#define __WORDSIZE 64
#endif
#if __WORDSIZE == 64
#define PRIxPTR "lx"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#else
#define PRIxPTR "x"
#define PRIdPTR "d"
#define PRIuPTR "u"
#endif

#endif
