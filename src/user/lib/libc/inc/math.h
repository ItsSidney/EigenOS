/* Minimal freestanding math.h for ring-3 user apps.
   Implementations live in math.c and use x87 instructions
   (fsqrt/fsin/fcos/fyl2x/f2xm1/fprem/fscale) for hardware accuracy. */
#ifndef EIGEN_SHIM_MATH_H
#define EIGEN_SHIM_MATH_H

#define M_PI  3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_E   2.71828182845904523536

#define isnan(x)   ((x) != (x))
#define isinf(x)   ((x) != 0.0 && (x) == (x) / 2.0)
#define signbit(x) ((x) < 0)

#ifdef __cplusplus
extern "C" {
#endif

double fabs(double x);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
 double log(double x);
 double log2(double x);
 double log10(double x);
double pow(double b, double e);
double fmod(double x, double y);
double modf(double x, double* ip);
double frexp(double x, int* exp);
double floor(double x);
double ceil(double x);
long   lround(double x);
double acos(double x);
double asin(double x);
double round(double x);

#define sqrtf(x)    ((float)sqrt((double)(x)))
#define sinf(x)     ((float)sin((double)(x)))
#define cosf(x)     ((float)cos((double)(x)))
#define tanf(x)     ((float)tan((double)(x)))
#define atan2f(y,x) ((float)atan2((double)(y),(double)(x)))
#define fabsf(x)    ((float)fabs((double)(x)))
#define floorf(x)   ((float)floor((double)(x)))
#define ceilf(x)    ((float)ceil((double)(x)))
#define expf(x)     ((float)exp((double)(x)))
#define logf(x)     ((float)log((double)(x)))
#define powf(b,e)   ((float)pow((double)(b),(double)(e)))
#define fmodf(x,y)  ((float)fmod((double)(x),(double)(y)))

#ifdef __cplusplus
}
#endif

#endif
/* freestanding fpclassify (used by upstream Evas/Ector code) */
#ifndef EIGEN_FPCLASSIFY
#define EIGEN_FPCLASSIFY
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (x))
#ifndef FP_NAN
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_NORMAL 2
#define FP_SUBNORMAL 3
#define FP_ZERO 4
#endif
#endif
