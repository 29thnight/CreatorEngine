// mathematics/arch/consteval_ops.hpp — what every operation *means*, defined once.
//
// Each SIMD backend selects instructions; this file defines the semantics those
// instructions must reproduce. It serves three roles at once:
//   1. the compile-time path of every SSE and NEON operation,
//   2. the entire scalar fallback backend,
//   3. the oracle the tests check the SIMD paths against.
// Because all three read the same definition, a backend cannot drift from the
// specification without a test noticing.
//
// Everything here is plain scalar arithmetic on lanes. Nothing in this file may
// call an intrinsic or a non-constexpr standard function.
#ifndef MATHEMATICS_ARCH_CONSTEVAL_OPS_HPP
#define MATHEMATICS_ARCH_CONSTEVAL_OPS_HPP

#include <mathematics/arch/reg.hpp>

namespace math::consteval_ops {

inline constexpr float quiet_nan = from_bits(0x7FC00000u);
inline constexpr float infinity = from_bits(0x7F800000u);

// ----------------------------------------------------------------- construction
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
set(float x, float y, float z, float w) noexcept {
    return make_reg(x, y, z, w);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg splat(float s) noexcept {
    return make_reg(s, s, s, s);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg zero() noexcept {
    return make_reg(0.0f, 0.0f, 0.0f, 0.0f);
}

// -------------------------------------------------------------------- lane-wise
// Every unary and binary operation is expressed through these two, fully
// unrolled with literal lane indices. Constant indices matter beyond style: NEON
// lane intrinsics require them, and a runtime index leaves an indexable array in
// the frame that trips MSVC's /GS analysis.
template <typename operation_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg unary(vec_reg a, operation_type op) noexcept {
    return make_reg(op(lane(a, 0)), op(lane(a, 1)), op(lane(a, 2)), op(lane(a, 3)));
}

template <typename operation_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
binary(vec_reg a, vec_reg b, operation_type op) noexcept {
    return make_reg(op(lane(a, 0), lane(b, 0)), op(lane(a, 1), lane(b, 1)),
                   op(lane(a, 2), lane(b, 2)), op(lane(a, 3), lane(b, 3)));
}

template <typename operation_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
ternary(vec_reg a, vec_reg b, vec_reg c, operation_type op) noexcept {
    return make_reg(op(lane(a, 0), lane(b, 0), lane(c, 0)),
                   op(lane(a, 1), lane(b, 1), lane(c, 1)),
                   op(lane(a, 2), lane(b, 2), lane(c, 2)),
                   op(lane(a, 3), lane(b, 3), lane(c, 3)));
}

// ------------------------------------------------------------------- arithmetic
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg add(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept { return x + y; });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg sub(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept { return x - y; });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg mul(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept { return x * y; });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg div(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept { return x / y; });
}

// Sign flipped by XOR rather than by 0 - x, so -0.0f negates to +0.0f and NaN
// payloads survive. This matches what the SIMD paths do.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg negate(vec_reg a) noexcept {
    return unary(a, [](float x) noexcept { return from_bits(bits_of(x) ^ sign_bit); });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg abs(vec_reg a) noexcept {
    return unary(a, [](float x) noexcept { return from_bits(bits_of(x) & abs_mask); });
}

// a * b + c. Written as separate operations, not std::fma, because std::fma is
// not constant-evaluable. The hardware FMA keeps the product at full width, so a
// SIMD result may differ from this in the last places -- tests compare mul_add
// with a tolerance for exactly that reason.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return ternary(a, b, c,
                   [](float x, float y, float z) noexcept { return x * y + z; });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
mul_sub(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return ternary(a, b, c,
                   [](float x, float y, float z) noexcept { return x * y - z; });
}

// c - a * b
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
neg_mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return ternary(a, b, c,
                   [](float x, float y, float z) noexcept { return z - x * y; });
}

// Written exactly as minps/maxps define themselves -- `a < b ? a : b`, not the
// equivalent-looking `b < a ? b : a`. The two agree on every ordered pair and
// differ only when an operand is NaN, since a comparison against NaN is false
// either way and the two forms then fall through to opposite operands. The
// result is that Min(NaN, x) is x and Min(x, NaN) is NaN.
//
// The transposed form passed random-input parity for exactly this reason and was
// caught only by the explicit NaN test in the scalar build.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg min(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept { return x < y ? x : y; });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg max(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept { return x > y ? x : y; });
}

// ------------------------------------------------------------ roots, reciprocals
// std::sqrt is not constant-evaluable before C++26, so this is Newton-Raphson in
// double precision. The seed halves the exponent in the bit pattern, which lands
// within a few percent and lets the iteration converge in a handful of steps
// regardless of magnitude. Compile-time cost is irrelevant; correctness is not.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float sqrt_scalar(float x) noexcept {
    if (x != x) return x;                       // NaN propagates unchanged
    if (x < 0.0f) return quiet_nan;             // domain error
    if (x == 0.0f) return x;                    // preserves signed zero
    if (x == infinity) return x;

    const float seed = from_bits((bits_of(x) >> 1) + 0x1FC00000u);
    double g = static_cast<double>(seed);
    const double v = static_cast<double>(x);
    for (int i = 0; i < 16; ++i) {
        const double next = 0.5 * (g + v / g);
        if (next == g) break;
        g = next;
    }
    return static_cast<float>(g);
}

// C++20 has no constexpr std::exp2. Split x into an integral power of two,
// represented exactly by its IEEE-754 exponent, and a fractional part in
// [0, 1). exp(frac * ln(2)) is evaluated in double with a degree-12 Taylor
// polynomial; the final cast performs the one rounding to float. The same
// implementation is used at run time so easing curves do not change merely
// because their progress was constant-evaluated.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float exp2_scalar(float x) noexcept {
    if (x != x) return x;
    if (x >= 128.0f) return infinity;
    if (x <= -150.0f) return 0.0f;

    int exponent = static_cast<int>(x);
    if (x < 0.0f && static_cast<float>(exponent) != x) --exponent;
    const double fraction = static_cast<double>(x - static_cast<float>(exponent));
    const double z = fraction * 0.693147180559945309417232121458176568;

    double polynomial = 0.000000002087675698786809897921009032;
    polynomial = 0.000000025052108385441718775052108385 + z * polynomial;
    polynomial = 0.000000275573192239858906525573192240 + z * polynomial;
    polynomial = 0.000002755731922398589065255731922399 + z * polynomial;
    polynomial = 0.000024801587301587301587301587301587 + z * polynomial;
    polynomial = 0.000198412698412698412698412698412698 + z * polynomial;
    polynomial = 0.001388888888888888888888888888888889 + z * polynomial;
    polynomial = 0.008333333333333333333333333333333333 + z * polynomial;
    polynomial = 0.041666666666666666666666666666666667 + z * polynomial;
    polynomial = 0.166666666666666666666666666666666667 + z * polynomial;
    polynomial = 0.5 + z * polynomial;
    polynomial = 1.0 + z * polynomial;
    polynomial = 1.0 + z * polynomial;

    float scale;
    if (exponent >= -126) {
        scale = from_bits(static_cast<std::uint32_t>(exponent + 127) << 23);
    } else {
        scale = from_bits(1u << static_cast<unsigned>(exponent + 149));
    }
    return static_cast<float>(static_cast<double>(scale) * polynomial);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg sqrt(vec_reg a) noexcept {
    return unary(a, [](float x) noexcept { return sqrt_scalar(x); });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg rsqrt(vec_reg a) noexcept {
    return unary(a, [](float x) noexcept { return 1.0f / sqrt_scalar(x); });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg recip(vec_reg a) noexcept {
    return unary(a, [](float x) noexcept { return 1.0f / x; });
}

// --------------------------------------------------------------------- bitwise
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg bit_and(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return from_bits(bits_of(x) & bits_of(y));
    });
}

// ~a & b, matching _mm_andnot_ps: the first operand is the one inverted.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg bit_and_not(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return from_bits(~bits_of(x) & bits_of(y));
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg bit_or(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return from_bits(bits_of(x) | bits_of(y));
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg bit_xor(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return from_bits(bits_of(x) ^ bits_of(y));
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg bit_not(vec_reg a) noexcept {
    return unary(a, [](float x) noexcept { return from_bits(~bits_of(x)); });
}

// ------------------------------------------------------------------ comparison
// Results are masks: every lane is all-ones or all-zeros (see reg.hpp).
namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float mask_of(bool b) noexcept {
    return from_bits(b ? lane_true : lane_false);
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg cmp_eq(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return detail::mask_of(x == y);
    });
}

// Ordered-false / unordered-true, matching cmpneq_ps: NaN compares not-equal to
// everything including itself.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg cmp_ne(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return detail::mask_of(!(x == y));
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg cmp_lt(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return detail::mask_of(x < y);
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg cmp_le(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return detail::mask_of(x <= y);
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg cmp_gt(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return detail::mask_of(x > y);
    });
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg cmp_ge(vec_reg a, vec_reg b) noexcept {
    return binary(a, b, [](float x, float y) noexcept {
        return detail::mask_of(x >= y);
    });
}

// Bitwise blend, not a branch: lanes where the mask is set come from if_true.
// A mask lane that is neither all-ones nor all-zeros blends bit by bit, which
// mirrors the hardware rather than pretending it cannot happen.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
select(mask_reg mask, vec_reg if_true, vec_reg if_false) noexcept {
    return ternary(mask, if_true, if_false,
                   [](float m, float t, float f) noexcept {
                       const std::uint32_t mb = bits_of(m);
                       return from_bits((bits_of(t) & mb) | (bits_of(f) & ~mb));
                   });
}

// ------------------------------------------------------------------- predicates
// One bit per lane, taken from each lane's sign bit -- the same definition as
// movmskps, so a mask lane of all-ones reads as set.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr int move_mask(vec_reg a) noexcept {
    return static_cast<int>(((lane_bits(a, 0) >> 31) << 0) |
                            ((lane_bits(a, 1) >> 31) << 1) |
                            ((lane_bits(a, 2) >> 31) << 2) |
                            ((lane_bits(a, 3) >> 31) << 3));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool all_true(mask_reg m) noexcept {
    return move_mask(m) == 0xF;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool any_true(mask_reg m) noexcept {
    return move_mask(m) != 0;
}

// ------------------------------------------------------------------- reductions
// Dot products splat the scalar result across all four lanes, matching
// DirectXMath, so the result feeds straight back into vector arithmetic without
// a broadcast step.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg dot2(vec_reg a, vec_reg b) noexcept {
    return splat(lane(a, 0) * lane(b, 0) + lane(a, 1) * lane(b, 1));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg dot3(vec_reg a, vec_reg b) noexcept {
    return splat(lane(a, 0) * lane(b, 0) + lane(a, 1) * lane(b, 1)
               + lane(a, 2) * lane(b, 2));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg dot4(vec_reg a, vec_reg b) noexcept {
    return splat(lane(a, 0) * lane(b, 0) + lane(a, 1) * lane(b, 1)
               + lane(a, 2) * lane(b, 2) + lane(a, 3) * lane(b, 3));
}

// ---------------------------------------------------------------------- shuffle
// Lane indices are template parameters because they must be compile-time
// constants: NEON's lane intrinsics and SSE's shuffle immediate both require it.
template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg shuffle(vec_reg a) noexcept {
    static_assert(x >= 0 && x < 4 && y >= 0 && y < 4 && z >= 0 && z < 4 &&
                      w >= 0 && w < 4,
                  "shuffle lane indices must be in [0, 4)");
    return make_reg(lane(a, x), lane(a, y), lane(a, z), lane(a, w));
}

// Two-source shuffle following _mm_shuffle_ps: the low two lanes come from a,
// the high two from b.
template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg shuffle(vec_reg a, vec_reg b) noexcept {
    static_assert(x >= 0 && x < 4 && y >= 0 && y < 4 && z >= 0 && z < 4 &&
                      w >= 0 && w < 4,
                  "shuffle lane indices must be in [0, 4)");
    return make_reg(lane(a, x), lane(a, y), lane(b, z), lane(b, w));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg splat_x(vec_reg a) noexcept {
    return shuffle<0, 0, 0, 0>(a);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg splat_y(vec_reg a) noexcept {
    return shuffle<1, 1, 1, 1>(a);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg splat_z(vec_reg a) noexcept {
    return shuffle<2, 2, 2, 2>(a);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg splat_w(vec_reg a) noexcept {
    return shuffle<3, 3, 3, 3>(a);
}

} // namespace math::consteval_ops

#endif // MATHEMATICS_ARCH_CONSTEVAL_OPS_HPP
