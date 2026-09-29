// (3) RALLY - maths that gives the same answer on every machine.
//
// The score server replays a stage with the game's own physics and expects
// the same drive, to the last bit, whether the game ran on a Mac, on Linux or
// in a browser. The C library's sin, cos, atan2, exp and pow differ between
// platforms in their last bits, and a last-bit difference can tip a gear
// change or a hit and send the whole drive elsewhere. These are built only
// from + - * /, sqrt, floor and exact power-of-two scaling, which IEEE-754
// makes identical everywhere (with -ffp-contract=off, so no fused
// multiply-adds), in double precision, so they are also accurate to ~1e-15.
// The physics (car.cpp) and the course (course.cpp) use these. (The course also sorts with
// std::stable_sort: plain std::sort orders equal items differently in each C++ library, and
// scenery on the same segment would come out in a different order on a Mac and on Linux.)
#pragma once
#include <cmath>

namespace rc::dm {

constexpr double PI_D = 3.14159265358979323846;

// sin on [-pi/2, pi/2]: Taylor series to x^19 (error below 1e-16 there).
inline double sinCore(double x) {
    const double x2 = x * x;
    double p = 1.0 / 121645100408832000.0;  // 1/19!
    p = p * x2 * -1 + 1.0 / 355687428096000.0;  // 1/17!
    p = p * x2 * -1 + 1.0 / 1307674368000.0;    // 1/15!
    p = p * x2 * -1 + 1.0 / 6227020800.0;       // 1/13!
    p = p * x2 * -1 + 1.0 / 39916800.0;         // 1/11!
    p = p * x2 * -1 + 1.0 / 362880.0;           // 1/9!
    p = p * x2 * -1 + 1.0 / 5040.0;             // 1/7!
    p = p * x2 * -1 + 1.0 / 120.0;              // 1/5!
    p = p * x2 * -1 + 1.0 / 6.0;                // 1/3!
    p = p * x2 * -1 + 1.0;
    return x * p;
}

inline double sinD(double x) {
    if (!std::isfinite(x)) return std::nan("");
    x -= std::floor(x / (2 * PI_D) + 0.5) * (2 * PI_D);  // into [-pi, pi]
    if (x > PI_D / 2) x = PI_D - x;
    else if (x < -PI_D / 2) x = -PI_D - x;
    return sinCore(x);
}
inline double cosD(double x) { return sinD(x + PI_D / 2); }

// atan: fold to [0, 1], halve the angle twice (to below tan(pi/16)), then a series.
inline double atanD(double x) {
    if (std::isnan(x)) return x;
    const bool neg = x < 0;
    double a = neg ? -x : x;
    bool inv = false;
    if (a > 1) {
        if (std::isinf(a)) return neg ? -PI_D / 2 : PI_D / 2;
        a = 1 / a;
        inv = true;
    }
    for (int k = 0; k < 2; k++) a = a / (1 + std::sqrt(1 + a * a));
    const double a2 = a * a;
    double p = 0;
    for (int n = 25; n >= 1; n -= 2) p = p * a2 + ((n / 2) % 2 ? -1.0 : 1.0) / n;  // 1 - a^2/3 + a^4/5 - ...
    double r = 4 * a * p;
    if (inv) r = PI_D / 2 - r;
    return neg ? -r : r;
}

inline double atan2D(double y, double x) {
    if (std::isnan(x) || std::isnan(y)) return std::nan("");
    if (x > 0) return atanD(y / x);
    if (x < 0) return y >= 0 ? atanD(y / x) + PI_D : atanD(y / x) - PI_D;
    return y > 0 ? PI_D / 2 : y < 0 ? -PI_D / 2 : 0.0;
}

// exp: x = k ln2 + r with |r| <= ln2/2, a series for e^r, then scale by 2^k (exact).
inline double expD(double x) {
    if (std::isnan(x)) return x;
    if (x > 700) return HUGE_VAL;
    if (x < -700) return 0;
    const double LN2 = 0.693147180559945309417;
    const double k = std::floor(x / LN2 + 0.5);
    const double r = x - k * LN2;
    double p = 1, term = 1;
    for (int n = 1; n <= 16; n++) {
        term = term * r / n;
        p += term;
    }
    return std::ldexp(p, int(k));
}

// log: x = m 2^e with m in [0.5, 1), and log m = 2 atanh((m - 1) / (m + 1)) as a series.
inline double logD(double x) {
    if (!(x > 0)) return x == 0 ? -HUGE_VAL : std::nan("");
    if (std::isinf(x)) return x;
    int e = 0;
    const double m = std::frexp(x, &e);
    const double z = (m - 1) / (m + 1), z2 = z * z;
    double p = 0;
    for (int n = 41; n >= 1; n -= 2) p = p * z2 + 1.0 / n;
    return 2 * z * p + e * 0.693147180559945309417;
}

inline float sin(float x) { return float(sinD(x)); }
inline float cos(float x) { return float(cosD(x)); }
inline float tan(float x) { return float(sinD(x) / cosD(x)); }
inline float atan(float x) { return float(atanD(x)); }
inline float atan2(float y, float x) { return float(atan2D(y, x)); }
inline float exp(float x) { return float(expD(x)); }
inline float pow(float a, float b) { return a > 0 ? float(expD(double(b) * logD(a))) : a == 0 ? (b > 0 ? 0.0f : 1.0f) : std::nanf(""); }

}  // namespace rc::dm
