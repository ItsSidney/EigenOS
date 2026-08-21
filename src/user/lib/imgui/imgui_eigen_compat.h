/* imgui_eigen_compat.h — EigenOS freestanding shims for Dear ImGui.
 *
 * Included BEFORE imgui.h / imgui_internal.h by the backend and app.
 * Bridges three gaps between ImGui's full-libm assumptions and EigenOS'
 * minimal ring-3 libc:
 *
 *   1. Missing math functions: EigenOS libc provides sin/cos/tan/
 *      atan2/sqrt/pow/log/fmod/floor/ceil/fabs but NOT acos.
 *      ImGui (rounded corners, arcs) needs acosf/acos. We implement them
 *      via atan2(sqrt(1-x*x), x) — both available. Declared inline so they
 *      coexist with any future libc additions.
 *
 *   2. No libstdc++: -fno-exceptions/-fno-rtti strips cxa_*. ImGui still
 *      needs operator new/delete, routed to eigen_malloc/eigen_free in the
 *      backend .cpp (single TU; declared extern here).
 *
 *   3. Static-archive ctors: ImGui's demo + internals reference __cxa_atexit
 *      and __dso_handle (the C++ runtime global-ctor chain). EigenOS' _start
 *      does not run .init_array. We declare them extern here; the backend
 *      .cpp defines no-op stubs so the archive links without libstdc++.
 *
 *   4. C-linkage libc: EigenOS' ring-3 libc headers (stdio.h, stdlib.h,
 *      string.h, math.h, etc.) are C-only (no extern "C" guards). Without
 *      this wrapper, a .cpp TU mangles printf/memcpy/cos as C++ symbols and
 *      they won't link against your C-compiled libc.o. This extern "C"
 *      block MUST come before imgui.h's own includes.
 *
 * NOTE: do NOT macro-redirect acosf — that would leak into every TU that
 * pulls math.h. These inline defs satisfy lookup. */
#ifndef IMGUI_EIGEN_COMPAT_H
#define IMGUI_EIGEN_COMPAT_H

/* Gap 4: force C ABI on EigenOS' C-only libc headers. Must precede imgui.h
 * so imgui's <stdio.h>/<stdlib.h>/<string.h>/<math.h> sees C-linkage decls. */
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __cplusplus
}
#endif

#include <math.h>  /* already has extern guard; cos/sin/sqrt are now C-linked */

#ifdef __cplusplus
extern "C" {
#endif

/* Gap 1: acos not in EigenOS libc. Provide it inline. */
static inline float  acosf_eigen(float  x) {
    if (x >=  1.0f) return 0.0f;
    if (x <= -1.0f) return (float)3.14159265358979323846;
    return (float)atan2(sqrtf(1.0f - x * x), x);
}
static inline double acos_eigen(double x) {
    if (x >=  1.0) return 0.0;
    if (x <= -1.0) return 3.14159265358979323846;
    return atan2(sqrt(1.0 - x * x), x);
}
/* Provide the names ImGui calls directly (acosf/acos). Guarded so a
 * future libc that adds real ones doesn't conflict. */
#ifndef EIGEN_HAVE_ACOS
inline float  acosf(float  x) { return acosf_eigen(x); }
inline double acos(double x)  { return acos_eigen(x); }
#define EIGEN_HAVE_ACOS
#endif

/* Gap 2: operator new/delete declared here, DEFINED in the backend .cpp.
 * (See imgui_impl_eigen.cpp for the eigen_malloc/eigen_free shims.) */

/* Gap 3: __cxa_atexit / __dso_handle — declared extern here, defined as
 * no-ops in the backend .cpp (single TU). Prevents multi-definition. */
extern int __cxa_atexit(void* fn, void* arg, void* dso);
extern void* __dso_handle;

#ifdef __cplusplus
}
#endif

#endif /* IMGUI_EIGEN_COMPAT_H */
