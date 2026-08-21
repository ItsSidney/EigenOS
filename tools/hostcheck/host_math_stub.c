/* host_math_stub.c — host-only bridge so the EigenOS libc object symbols
 * resolve during a host link-check. Provides the few math/string symbols
 * ImGui + our backend reference that your ring-3 libc.o/math.o would
 * supply in the real build. Wraps them to the host libc. NEVER in ISO. */
#include <math.h>
#include <string.h>

/* math.h on host has these natively; just make sure they're visible */
double sqrt(double x)   { return __builtin_sqrt(x); }
double atan2(double y, double x) { return __builtin_atan2(y, x); }
double fabs(double x)   { return __builtin_fabs(x); }
