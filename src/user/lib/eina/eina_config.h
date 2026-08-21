/* eina_config.h — generated stub for the EigenOS freestanding port of Eina.
 * Mirrors the subset of options that Eina's headers gate on. These values
 * are chosen for the single-file ring-3 build (no meson). */
#ifndef EINA_CONFIG_H
#define EINA_CONFIG_H

#include <stddef.h>  /* size_t (GCC built-in header) */
#include <sched.h>   /* cpu_set_t / CPU_ZERO / sched_getaffinity for eina_cpu.c */

/* --- features we support --- */
#define EINA_ENABLE_LOG              1
#define EINA_SAFETY_CHECKS           1
#define EINA_HAVE_THREADS            1
#define EFL_HAVE_THREADS             1
#define EINA_HAVE_PTHREAD_BARRIERS   1   /* pthread_barrier_* on Linux */
#define EINA_HAVE_PTHREAD_SETNAME    1   /* pthread_setname_np */
 #define EINA_CONFIGURE_HAVE_DIRENT_H 1
 #define EINA_HAVE_ALLOCA_H           1
 #define EINA_HAVE_BYTESWAP_H         1
 #define EINA_STATIC_BUILD_PASS_THROUGH 1   /* register malloc-backed mempool */

/* --- features we deliberately disable (no kernel support) --- */
/* #undef EINA_MAGIC_DEBUG */        /* keep off: magic-cookie overhead */
/* #undef EINA_COW_MAGIC_ON */        /* copy-on-write debug */
/* #undef EINA_DEBUG_MALLOC */        /* guarded allocator */
 #define EINA_DEFAULT_MEMPOOL         1   /* all modules use malloc-backed pass_through */
/* #undef EINA_STRINGSHARE_USAGE */   /* usage stats */
/* #undef EINA_HAVE_DEBUG_THREADS */
/* #undef EINA_LOG_COLOR_DEFAULT */
/* #undef EINA_LOG_BACKTRACE */
/* #undef NVALGRIND */                /* harmless if absent */

/* --- type sizes --- */
#define EINA_SIZEOF_WCHAR_T      1   /* <4 forces Eina to use uint32_t, avoiding <wchar.h> */
#define EINA_SIZEOF_UINTPTR_T    8

/* Module/install paths (used by eina_mempool.c). No real module loading
 * happens in the freestanding port, so these are dummies. */
#define MODULE_ARCH     "x86_64"
#define PACKAGE_LIB_DIR "/lib"

/* EFL package version (Eina's eina_main.c builds Eina_Version from these). */
#define VMAJ 1
#define VMIN 26
#define VMIC 3
#define VREV 0
#define EFL_VERSION_MAJOR 1
#define EFL_VERSION_MINOR 26
#define EFL_VERSION_MICRO 3

/* Max concurrent threads Eina tracks (mirrors eina_cpu.c's linux/glibc branch). */
#ifndef TH_MAX
#define TH_MAX 32
#endif

/* --- memory pool backends (we use the default malloc/chunked ones) --- */
#define EINA_SLSTR_DEFAULT_SIZE 256

#endif /* EINA_CONFIG_H */
