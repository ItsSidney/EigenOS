/* assert.h for ring-3 apps: failure aborts the process. */
#ifndef EIGEN_SHIM_ASSERT_H
#define EIGEN_SHIM_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

void abort(void);

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__))
void __assert_fail(const char* expr, const char* file, int line);
#endif

#ifdef __cplusplus
}
#endif

#endif