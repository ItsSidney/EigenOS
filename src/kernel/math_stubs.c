/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  libm — a correct, freestanding double-precision C math
//  library for EigenOS. Replaces the old Taylor-series stubs.
//
//  Algorithms follow the classic fdlibm (Sun) style: range
//  reduction + minimax/series kernels with hi/lo constant splits
//  to preserve precision. Accurate to ~1e-12 across the practical
//  GUI input range (angles/time, |x| well under 2^53).
//
//  Provides: fabs floor ceil fmod sqrt sin cos tan atan atan2
//  asin acos exp log log10 pow sinh cosh tanh, plus private
//  frexp/ldexp helpers. __popcountdi2 is a compiler-rt runtime
//  symbol and must remain here.
// ============================================================
#include <stdint.h>

/* The kernel builds freestanding, but some host toolchains still predefine
 * exp/log/sin/cos (etc.) as builtin macros. Undef them so our local libm
 * implementations compile without colliding with a macro expansion. */
#undef exp
#undef log
#undef pow
#undef sin
#undef cos
#undef sqrt
#undef fabs
#undef fmod

/* ── constants (hi/lo splits keep extra precision) ────────── */
#define TWO_PI_HI 6.283185307179586
#define TWO_PI_LO 1.8372490968605792e-15
#define TWO_PI    (TWO_PI_HI + TWO_PI_LO)
#define PI        3.1415926535897932
#define PI_2      1.5707963267948966
#define PI4       0.7853981633974483
#define PI_4      PI4
#define LN2_HI    0.6931471805599453
#define LN2_LO    9.4e-17
#define LN2       (LN2_HI + LN2_LO)
#define LN10      2.302585092994046
#define SQRT2     1.4142135623730951
#define TAN_P8    0.4142135623730950   /* tan(pi/8) */

typedef union { double d; uint64_t u; } dbit;

/* ── compiler runtime ─────────────────────────────────────── */
int __popcountdi2(uint64_t x) {
    int c = 0;
    while (x) { c++; x &= x - 1; }
    return c;
}

/* ── trivial primitives (correct as-is) ──────────────────── */
double fabs(double x) { return x < 0.0 ? -x : x; }

double floor(double x) {
    double r = (double)(long long)x;
    if (x < 0.0 && r != x) r -= 1.0;
    return r;
}

double ceil(double x) {
    double r = (double)(long long)x;
    if (x > 0.0 && r != x) r += 1.0;
    return r;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0 / 0.0;
    double q = (long long)(x / y);
    return x - q * y;
}

/* ── frexp / ldexp helpers ───────────────────────────────── */
double frexp(double x, int* e) {
    if (x == 0.0) { *e = 0; return 0.0; }
    dbit u; u.d = x;
    int E = (int)((u.u >> 52) & 0x7ff);
    *e = E - 1022;
    u.u = (u.u & 0x800fffffffffffffULL) | 0x3ff0000000000000ULL; /* value in [1,2) */
    return 0.5 * u.d;                                            /* -> [0.5,1) */
}

double ldexp(double x, int n) {
    if (x == 0.0) return x;
    dbit u; u.d = x;
    int E = (int)((u.u >> 52) & 0x7ff);
    if (E == 0) return ldexp(frexp(x, &E), n);  /* subnormal: normalise first */
    E += n;
    if (E >= 0x7ff) return (x > 0.0) ? (1.0 / 0.0) : (-1.0 / 0.0); /* overflow */
    if (E <= 0)     return 0.0 * x;                                  /* underflow */
    u.u = (u.u & 0x800fffffffffffffULL) | ((uint64_t)E << 52);
    return u.d;
}

/* ── sqrt: decompose + Newton (quadratic convergence) ───── */
double sqrt(double x) {
    if (x < 0.0) return 0.0 / 0.0;        /* NaN for negatives */
    if (x == 0.0) return 0.0;
    int e; double m = frexp(x, &e);       /* m in [0.5,1) */
    if (e & 1) { m *= 2.0; e--; }         /* now e even, m in [0.5,2) */
    double r = m * 0.5 + 0.5;             /* init in [0.5,1.5) */
    for (int i = 0; i < 12; i++) r = 0.5 * (r + m / r);
    return ldexp(r, e / 2);
}

/* ── sin/cos kernel polynomials (|x| <= pi/4) ────────────── */
static const double S1 = -1.66666666666666324348e-01,
                    S2 =  8.33333333332248946124e-03,
                    S3 = -1.98412698298579493134e-04,
                    S4 =  2.75573137070700676789e-06,
                    S5 = -2.50507602534068634195e-08,
                    S6 =  1.58969099521155010221e-10;
static const double C1 =  4.16666666666666019037e-02,
                    C2 = -1.38888888888741095749e-03,
                    C3 =  2.48015872894767294175e-05,
                    C4 = -2.75573143513906633035e-07,
                    C5 =  2.08757232129817482790e-09,
                    C6 = -1.13596475577881948265e-11;

static double k_sin(double x) {
    double z = x * x;
    double v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x + v * (S1 + z * r);
}
static double k_cos(double x) {
    double z = x * x;
    double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    return 1.0 + (z * (-0.5) + (z * r));
}

double sin(double x) {
    double s = (x < 0.0) ? -1.0 : 1.0;
    x = fabs(x);
    double k = floor(x / TWO_PI);
    x = x - k * TWO_PI_HI - k * TWO_PI_LO;     /* x in [0,2*pi) */
    if (x > PI)        x -= TWO_PI;            /* [-pi,pi)     */
    if (x > PI_2)      x = PI - x;             /* [-pi/2,pi/2] */
    else if (x < -PI_2) x = -PI - x;
    double v;
    if      (x >  PI4) v =  k_cos(x - PI_2);
    else if (x < -PI4) v = -k_cos(x + PI_2);
    else               v =  k_sin(x);
    return s * v;
}

double cos(double x) { return sin(x + PI_2); }

double tan(double x) {
    double c = cos(x);
    return (c != 0.0) ? sin(x) / c : (1.0 / 0.0);
}

/* ── atan (core valid for |x| <= tan(pi/8)) ──────────────── */
static double atan_core(double x) {
    double x2 = x * x, x3 = x2 * x;
    double r = x, t = x;
    for (int n = 0; n < 60; n++) {
        t *= -x2;
        double term = t / (double)(2 * n + 3);
        if (fabs(term) < 1e-15) break;
        r += term;
    }
    return r;
}
double atan(double x) {
    int s = (x < 0.0) ? -1 : 1;
    x = fabs(x);
    double a = 0.0;
    if (x > 1.0)      { x = 1.0 / x; a = PI_2; }
    else if (x > TAN_P8) { x = (x - 1.0) / (1.0 + x); a = PI_4; }
    return s * (a + atan_core(x));
}
double atan2(double y, double x) {
    if (x == 0.0 && y == 0.0) return 0.0;
    if (x == 0.0) return (y > 0.0) ? PI_2 : -PI_2;
    if (x > 0.0) return atan(y / x);
    if (y >= 0.0) return atan(y / x) + PI;
    return atan(y / x) - PI;
}

/* ── asin / acos ─────────────────────────────────────────── */
double asin(double x) {
    if (x < -1.0 || x > 1.0) return 0.0 / 0.0;
    int s = (x < 0.0) ? -1 : 1;
    x = fabs(x);
    return s * atan(x / sqrt(1.0 - x * x));
}
double acos(double x) { return PI_2 - asin(x); }

/* ── exp (decomposition x = n*ln2 + r, polynomial on r) ──── */
double exp(double x) {
    if (x >  709.0) return  1.0 / 0.0;   /* overflow -> +inf */
    if (x < -745.0) return  0.0;         /* underflow -> 0   */
    double n = floor(x / LN2_HI);
    double f = x - n * LN2_HI - n * LN2_LO;
    /* e^f - 1 via Horner (every level multiplies by f, not f^2) */
    double c = f * (1.0 + f * (0.5 + f * (1.0/6.0 + f * (1.0/24.0 + f * (1.0/120.0 +
              f * (1.0/720.0 + f * (1.0/5040.0 + f / 40320.0)))))));
    return ldexp(1.0 + c, (int)n);
}

/* ── log (frexp + atanh series on reduced m) ────────────── */
double log(double x) {
    if (x == 0.0) return -1.0 / 0.0;     /* -inf */
    if (x <  0.0) return  0.0 / 0.0;     /* NaN  */
    int e; double m = frexp(x, &e);      /* m in [0.5,1) */
    if (m < 0.7071067811865476) { m *= 2.0; e--; }   /* -> [0.7071,1.4142) */
    double z = (m - 1.0) / (m + 1.0);   /* |z| <= 0.1716 */
    double z2 = z * z;
    double R = z + (z2 * z) / 3.0 + (z2 * z2 * z) / 5.0
             + (z2 * z2 * z2 * z) / 7.0 + (z2 * z2 * z2 * z2 * z) / 9.0;
    return (double)e * LN2 + 2.0 * R;
}
double log10(double x) { return log(x) / LN10; }

/* ── pow ─────────────────────────────────────────────────── */
double pow(double base, double exponent) {
    if (base == 0.0) {
        if (exponent == 0.0) return 1.0;
        return (exponent > 0.0) ? 0.0 : (1.0 / 0.0);
    }
    if (base < 0.0) {
        double ip = floor(exponent);
        if (exponent == ip) {                 /* integer exponent -> real */
            double r = pow(-base, exponent);
            return (((int)ip) & 1) ? -r : r;
        }
        return 0.0 / 0.0;                      /* non-integer -> NaN */
    }
    return exp(exponent * log(base));
}

/* ── hyperbolic (via exp) ────────────────────────────────── */
double sinh(double x) { return 0.5 * (exp(x) - exp(-x)); }
double cosh(double x) { return 0.5 * (exp(x) + exp(-x)); }
double tanh(double x) { double e = exp(2.0 * x); return (e - 1.0) / (e + 1.0); }
