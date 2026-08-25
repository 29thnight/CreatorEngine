// mathematics/scalar.hpp — scalar constants and transcendentals.
//
// These exist because the rest of Phase 4 needs them: an axis-angle quaternion
// needs sine and cosine, Euler extraction needs arc tangent, a perspective
// matrix needs a tangent, and slerp needs an arc cosine. The standard library
// has all of them and none of them are constant-evaluable before C++26, so a
// library that promises `constexpr` everywhere has to supply its own.
//
// ONE implementation, used at compile time and at run time alike. The obvious
// alternative -- a polynomial for constant evaluation and std::sin at run time,
// the way scalar_sqrt is split -- would make `constexpr quaternion q =
// from_axis_angle(a, t)` hold visibly different numbers than the same call in a
// function body. Phase 3 spent a long day on exactly that shape of divergence in
// inverse (docs/PLAN.md, rule 7), and there the two implementations at least had
// a reason to exist. Here they would not: these are the same minimax
// approximations DirectXMath evaluates at run time, so using them everywhere
// costs nothing against the baseline and keeps the two contexts as close as the
// language permits.
//
// As close as permitted is not identical, and the tests measure the gap rather
// than assume it away. Clang contracts the polynomials' multiply-adds into FMA
// instructions at run time; constant evaluation never contracts. So a value
// computed at compile time and the same value computed at run time can differ by
// one ULP there, and do not differ at all on MSVC or GCC. One ULP is the floor
// while the compiler is free to fuse -- no choice of implementation gets below
// it -- and the tests assert that bound instead of an epsilon, so a real
// divergence could not hide inside a generous tolerance.
//
// Accuracy, measured against a double-precision reference and asserted by the
// tests, so these numbers cannot quietly stop being true:
//
//   Sin, Cos      2.7e-07 absolute, flat across the whole admissible range
//   asin, acos    4.2e-07 absolute
//   atan, atan2   4.6e-07 absolute
//   Tan           1.7e-06 relative, |x| < 1.5
//
// Float epsilon is 1.2e-07, so every one of these is within a few ULP of the
// best a float result can be. "Flat" is worth the word: the naive versions of
// both the range reduction and the arc tangent were accurate near zero and fell
// apart away from it -- 3.1e-02 for Sin at large angles, 3.5e-04 for atan at
// large arguments -- which is the failure mode that survives a test suite built
// only from small inputs.
#ifndef MATHEMATICS_SCALAR_HPP
#define MATHEMATICS_SCALAR_HPP

#include <mathematics/arch/consteval_ops.hpp>

#include <cmath>

namespace math {

// ------------------------------------------------------------------ constants
inline constexpr float pi        = 3.141592654f;
inline constexpr float two_pi     = 6.283185307f;
inline constexpr float half_pi    = 1.570796327f;
inline constexpr float quarter_pi = 0.785398163f;
inline constexpr float inv_pi     = 0.318309886f;
inline constexpr float inv_two_pi  = 0.159154943f;

inline constexpr float deg_to_rad = 0.01745329252f;
inline constexpr float rad_to_deg = 57.29577951f;

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float radians(float degrees) noexcept {
    return degrees * deg_to_rad;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float degrees(float radians) noexcept {
    return radians * rad_to_deg;
}

// -------------------------------------------------------------- small helpers
namespace detail {

// The comparison form, which compiles to a single andps and stays in the
// vector registers.
//
// The bitwise spelling the vector layer's Abs uses -- `from_bits(bits_of(x) &
// abs_mask)` -- is the more obviously correct one, because `x < 0` is false
// for negative zero and so this form hands -0.0f straight back. It was tried
// here and cost 22% of Slerp and 8% of sin_cos: bit_cast on x86 round-trips the
// value through a general-purpose register, and Reducible() calls this on
// every single trigonometric evaluation.
//
// It is safe because no caller can reach it with a zero. asin and atan return
// small arguments untouched before this is called, which is what preserves
// their signed zeros, and Reducible cannot tell the two zeros apart anyway.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float abs_scalar(float x) noexcept {
    return x < 0.0f ? -x : x;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool sign_bit_set(float x) noexcept {
    return (bits_of(x) & sign_bit) != 0u;
}

// The one square root the scalar layer needs, split the way the rest of the
// library splits it: consteval_ops::sqrt_scalar hand-rolls Newton-Raphson in
// double because std::sqrt is not constant-evaluable, and paying for that at run
// time is a measured disaster -- 4.6x, when it slipped into normalize during
// Phase 2. Defined here rather than in vector_common.hpp because the inverse
// trigonometric functions below need it and must not drag in the vector layer.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float scalar_sqrt(float x) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::sqrt_scalar(x); }
    return std::sqrt(x);
}

// True when the argument is finite and within the range the reduction is
// validated over.
//
// The reduction itself no longer sets this bound -- it is one double-precision
// multiply and subtract, exact far beyond it, and the int conversion it feeds
// would not overflow until |x| is past 1e10 (overflow there would be undefined
// behaviour, which during constant evaluation is a hard compile error, so a
// bound must exist somewhere). 8.2e5 is kept for two honest reasons: it is the
// range the header's accuracy claim is actually measured over, and by 2^22 the
// spacing between adjacent float arguments reaches half a radian -- an input
// that far out names its own angle too coarsely for any answer to mean much.
// Rejecting with NaN at a bound we can honour beats returning noise.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool reducible(float x) noexcept {
    return x - x == 0.0f && abs_scalar(x) < 8.2e5f;
}

} // namespace detail

// ------------------------------------------------------------ sine and cosine
// Range reduction to [-pi/2, pi/2] followed by the minimax polynomials
// DirectXMath uses in XMScalarSinCos: 11 degrees for sine, 10 for cosine, both
// odd/even in the reduced argument so they evaluate in y squared.
namespace detail {

struct sin_cos_pair {
    float sin;
    float cos;
};

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr sin_cos_pair
sin_cos_impl(float radians) noexcept {
    if (!reducible(radians)) return sin_cos_pair{consteval_ops::quiet_nan,
                                               consteval_ops::quiet_nan};

    // Nearest multiple of 2pi, removed. Rounding through int rather than a
    // library round keeps this constant-evaluable.
    const float scaled = radians * inv_two_pi;
    const float quotient = static_cast<float>(
        static_cast<int>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f));

    // The subtraction happens in double, and that is the whole reason this
    // function is accurate away from zero.
    //
    // In float, `radians - quotient * two_pi` is hopeless: two_pi is 2pi rounded
    // to 24 bits, so it is already off by ~5e-8, and multiplying that error by a
    // quotient in the thousands puts more error into the reduced angle than the
    // polynomial's own. The polynomial then faithfully computes the sine of the
    // wrong number. Measured worst error over |x| <= 1000: 5.4e-06 that way.
    // Splitting 2pi into two floats (Cody-Waite) gets it to 2.8e-07, but decays
    // again past |x| ~ 1e5 as the products stop being exact.
    //
    // One double multiply and subtract removes the problem outright rather than
    // pushing it further out: 53 bits of mantissa leave the reduced angle good
    // to ~1e-10 across the entire admissible range, and on every target this
    // library builds for, double arithmetic is hardware. Measured 1.5e-07 worst,
    // flat from |x| = 1 to the reduction limit.
    constexpr double two_pi_exact = 6.28318530717958647692528676655900577;
    const float y0 = static_cast<float>(static_cast<double>(radians) -
                                        static_cast<double>(quotient) * two_pi_exact);
    float y = y0;

    // Fold [-pi, pi] into [-pi/2, pi/2]. Sine is unchanged by the reflection,
    // cosine changes sign, which is what `sign` carries.
    float sign = 1.0f;
    if (y > half_pi) {
        y = pi - y;
        sign = -1.0f;
    } else if (y < -half_pi) {
        y = -pi - y;
        sign = -1.0f;
    }

    const float y2 = y * y;

    const float s = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2
                        - 0.00019840874f) * y2 + 0.0083333310f) * y2
                      - 0.16666667f) * y2 + 1.0f) * y;

    const float c = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2
                       - 0.0013888378f) * y2 + 0.041666638f) * y2
                     - 0.5f) * y2 + 1.0f;

    return sin_cos_pair{s, sign * c};
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float sin(float radians) noexcept {
    return detail::sin_cos_impl(radians).sin;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float cos(float radians) noexcept {
    return detail::sin_cos_impl(radians).cos;
}

// Both at once. Every rotation constructor needs the pair, and the reduction --
// the expensive half -- is shared, so asking for them separately does the work
// twice.
MATHEMATICS_INLINE constexpr void sin_cos(float radians, float& sin_out,
                                   float& cos_out) noexcept {
    const detail::sin_cos_pair p = detail::sin_cos_impl(radians);
    sin_out = p.sin;
    cos_out = p.cos;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float tan(float radians) noexcept {
    const detail::sin_cos_pair p = detail::sin_cos_impl(radians);
    return p.sin / p.cos;
}

// --------------------------------------------------------------- inverse trig
// Seven-degree minimax in |x|, scaled by sqrt(1 - |x|), again following
// DirectXMath. The square root is what carries the vertical tangent at the ends
// of the domain, where a plain polynomial in x cannot follow the curve.
namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float acos_core(float x) noexcept {
    const float p = ((((((-0.0012624911f * x + 0.0066700901f) * x
                         - 0.0170881256f) * x + 0.0308918810f) * x
                       - 0.0501743046f) * x + 0.0889789874f) * x
                     - 0.2145988016f) * x + 1.5707963050f;
    return p * scalar_sqrt(1.0f - x);
}

} // namespace detail

// Out-of-domain input is clamped rather than turned into NaN. A dot product of
// two unit vectors is the usual argument here and rounding routinely pushes it a
// few ULP past one; a NaN at that point would be a bug report, not a diagnosis.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float acos(float value) noexcept {
    if (value != value) return consteval_ops::quiet_nan;
    const float v = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
    const bool non_negative = v >= 0.0f;
    const float x = detail::abs_scalar(v);
    const float r = detail::acos_core(x);
    return non_negative ? r : pi - r;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float asin(float value) noexcept {
    if (value != value) return consteval_ops::quiet_nan;
    // Small arguments return themselves. Two reasons share the branch. First,
    // asin(x) == x to full float precision below 2^-11 -- the next Taylor term
    // is x^3/6, a relative x^2/6 < 4e-8, under float epsilon -- while the
    // polynomial path computes half_pi - acos_core(x), a subtraction of two
    // values near 1.5708 whose cancellation costs ~2e-7 ABSOLUTE error: fine
    // against the documented absolute bound, but a 25% RELATIVE error at
    // x = 1e-6, which turned a tiny rotation's angle into noise in
    // to_axis_angle. Second, asin(+/-0) is +/-0 with the sign preserved, per
    // the C library, and returning the argument is what preserves it.
    if (value > -4.8828125e-4f && value < 4.8828125e-4f) return value;
    const float v = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
    const bool non_negative = v >= 0.0f;
    const float x = detail::abs_scalar(v);
    const float r = half_pi - detail::acos_core(x);
    return non_negative ? r : -r;
}

// Arc tangent by way of the identity atan(x) = asin(x / sqrt(1 + x*x)), which
// reuses the polynomial above instead of introducing a fourth one.
//
// Only for |x| <= 1. Beyond that the identity is numerically useless: the
// argument to asin closes on +/-1, where the arc sine's slope runs to infinity,
// so a rounding error of one ULP in the argument becomes an unbounded error in
// the answer. Measured 3.5e-04 at |x| = 5000 before the fold below was added.
// Reflecting through atan(x) = pi/2 - atan(1/x) keeps the argument in the half
// of the domain where the identity is well conditioned, and costs one divide.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float atan(float value) noexcept {
    if (value != value) return consteval_ops::quiet_nan;
    if (value == consteval_ops::infinity) return half_pi;
    if (value == -consteval_ops::infinity) return -half_pi;
    // Small arguments return themselves, exactly as in asin and for the same
    // pair of reasons -- atan(x) == x to float precision below 2^-11 (next
    // term x^3/3), and the signed zero comes through intact, which is what
    // lets atan2(-0, +x) and atan2(-y, +inf) land on the right side of zero.
    if (value > -4.8828125e-4f && value < 4.8828125e-4f) return value;

    const float a = detail::abs_scalar(value);
    const float x = a > 1.0f ? 1.0f / a : a;
    float r = asin(x / detail::scalar_sqrt(1.0f + x * x));
    if (a > 1.0f) r = half_pi - r;
    return value < 0.0f ? -r : r;
}

// Quadrant-correct, and defined at the axes and at the origin the way the C
// library defines them, so callers do not have to special-case a zero component.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float atan2(float y, float x) noexcept {
    if (y != y || x != x) return consteval_ops::quiet_nan;

    // Both infinite: the one case the division below cannot express. inf/inf
    // is NaN by IEEE-754, which would fall into atan's NaN guard and poison
    // the answer -- while std::atan2 defines these as the four quadrant
    // diagonals. Every other infinite mix survives the division: y infinite
    // with x finite divides to +/-inf, finite y over infinite x divides to a
    // signed zero, and atan handles both ends.
    if (detail::abs_scalar(y) == consteval_ops::infinity &&
        detail::abs_scalar(x) == consteval_ops::infinity) {
        const float magnitude = x > 0.0f ? quarter_pi : 3.0f * quarter_pi;
        return detail::sign_bit_set(y) ? -magnitude : magnitude;
    }

    if (x == 0.0f) {
        if (y > 0.0f) return half_pi;
        if (y < 0.0f) return -half_pi;
        // Both zero: sign of x decides, matching std::atan2's treatment of the
        // signed zero.
        return detail::sign_bit_set(x) ? pi : 0.0f;
    }

    const float base = atan(y / x);
    if (x > 0.0f) return base;
    // Left half plane: shift by pi, toward whichever side y sits on. y == 0 with
    // x negative is +pi by convention on a positive zero.
    return detail::sign_bit_set(y) ? base - pi : base + pi;
}

} // namespace math

#endif // MATHEMATICS_SCALAR_HPP
