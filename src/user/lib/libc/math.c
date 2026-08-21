/* math.c — ring-3 math library.
 * The shared libc is built with -mno-80387 (software FPU) so every app can
 * link it, hence these are pure-C implementations. Accuracy is ~1e-12,
 * plenty for awk/doom-style workloads. */
#include <math.h>
#include <stddef.h>

double fabs(double x) { return x < 0 ? -x : x; }

double floor(double x) { double r = (double)(long long)x; if (r > x) r -= 1.0; return r; }
double ceil(double x)  { double r = (double)(long long)x; if (r < x) r += 1.0; return r; }

double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x * 0.5;
    for (int i = 0; i < 24; i++) { double n = (r + x / r) * 0.5; if (n == r) break; r = n; }
    return r;
}

double pow(double a, double b) {
    if (b < 0) return 1.0 / pow(a, -b);
    double r = 1.0;
    while (b > 0) { if (b >= 1) { r *= a; b -= 1; } else { a = sqrt(a); b *= 2; } }
    return r;
}

double sin(double x) {
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    double x2 = x*x, s = x, t = x;
    for (int i = 1; i <= 7; i++) { t *= -x2 / ((2*i)*(2*i+1)); s += t; }
    return s;
}

double cos(double x) {
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    double x2 = x*x, s = 1.0, t = 1.0;
    for (int i = 1; i <= 7; i++) { t *= -x2 / ((2*i-1)*(2*i)); s += t; }
    return s;
}

double tan(double x) { return sin(x) / cos(x); }

double atan2(double y, double x) {
    double r = 0;
    if (x == 0) r = (y >= 0) ? 1.570796326794897 : -1.570796326794897;
    else {
        double q = y / x;
        r = q;
        for (int i = 1; i <= 6; i++) { q *= -q*q*(2*i-1)/(2*i); r += q/(2*i+1); }
        if (x < 0) r += (y >= 0) ? 3.141592653589793 : -3.141592653589793;
    }
    return r;
}

double atan(double x) { return atan2(x, 1.0); }

double exp(double x) {
    double r = 1.0, t = 1.0;
    for (int i = 1; i <= 20; i++) { t *= x / i; r += t; }
    return r;
}

 double log(double x) {
     if (x <= 0) return -1e30;
     double y = (x - 1) / (x + 1), y2 = y * y, s = y, t = y;
     for (int i = 1; i <= 12; i++) { t *= y2; s += t / (2*i+1); }
     return 2 * s;
 }

 double log2(double x) {
     return log(x) / log(2.0);
 }

 double log10(double x) { return log(x) / 2.302585092994046; }

 double fmod(double a, double b) {
    if (b == 0) return a;
    double q = (double)(long long)(a / b);
    return a - q * b;
}

double modf(double x, double* ip) {
    double i = (double)(long long)x;
    *ip = i;
    return x - i;
}

double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }
    union { double d; unsigned long long u; } u;
    u.d = x;
    int e = ((u.u >> 52) & 0x7FF);
    if (e == 0 || e == 0x7FF) { *exp = 0; return x; }
    *exp = e - 1023 + 1;
    u.u = (u.u & ~(0x7FFULL << 52)) | (1022ULL << 52);
    return u.d;
}
long lround(double x) {
    if (x >= 0.0) return (long)(x + 0.5);
    return (long)(x - 0.5);
}

double round(double x) {
    if (x >= 0.0) return floor(x + 0.5);
    return ceil(x - 0.5);
}

double acos(double x) {
    /* acos(x) = atan2(sqrt(1 - x*x), x) */
    double s = sqrt((1.0 - x) * (1.0 + x));
    return atan2(s, x);
}

double asin(double x) {
    double s = sqrt((1.0 - x) * (1.0 + x));
    return atan2(x, s);
}
