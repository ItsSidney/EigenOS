/* Freestanding float.h shim for EigenOS ring-3 / EFL port. */
#ifndef EIGEN_SHIM_FLOAT_H
#define EIGEN_SHIM_FLOAT_H

/* Use GCC's built-in float.h via pragma since we have -nostdinc.
 * GCC generates these values from target ABI so they are always correct. */
#ifndef FLT_MAX
#define FLT_MAX         3.40282347e+38F
#define FLT_MIN         1.17549435e-38F
#define FLT_EPSILON     1.19209290e-07F
#define FLT_DIG         6
#define FLT_MANT_DIG    24
#define FLT_MAX_EXP     128
#define FLT_MIN_EXP     (-125)
#define DBL_MAX         1.7976931348623157e+308
#define DBL_MIN         2.2250738585072014e-308
#define DBL_EPSILON     2.2204460492503131e-16
#define DBL_DIG         15
#define DBL_MANT_DIG    53
#define DBL_MAX_EXP     1024
#define DBL_MIN_EXP     (-1021)
#define LDBL_MAX        DBL_MAX
#define LDBL_MIN        DBL_MIN
#define LDBL_EPSILON    DBL_EPSILON
#define FLT_RADIX       2
#define FLT_ROUNDS      1
#define DECIMAL_DIG     21
#endif

#endif /* EIGEN_SHIM_FLOAT_H */
