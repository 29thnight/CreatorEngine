// mathematics/arch/simd_scalar.hpp — portable fallback backend.
//
// Selected when no SIMD backend fits the target, or forced with
// MATHEMATICS_FORCE_SCALAR so CI keeps this path compiled and tested. It is a
// correctness fallback, not a performance one: expect several times the cost of
// the SSE or NEON backends.
//
// Almost everything forwards straight to consteval_ops, which already defines
// the semantics in plain scalar arithmetic. The exceptions are the square-root
// family, where consteval_ops must hand-roll Newton-Raphson because std::sqrt is
// not constant-evaluable -- at runtime that would be needlessly slow, so the
// standard library is used instead.
#ifndef MATHEMATICS_ARCH_SIMD_SCALAR_HPP
#define MATHEMATICS_ARCH_SIMD_SCALAR_HPP

#include <mathematics/arch/consteval_ops.hpp>

#include <cmath>

namespace math {

// ---------------------------------------------------------------- construction
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
set(float x, float y, float z, float w) noexcept {
    return consteval_ops::set(x, y, z, w);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat(float s) noexcept {
    return consteval_ops::splat(s);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL zero() noexcept {
    return consteval_ops::zero();
}

// ------------------------------------------------------------------ load, store
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load(const float* p) noexcept {
    return make_reg(p[0], p[1], p[2], p[3]);
}

// No alignment requirement to exploit without vector instructions; kept for
// interface parity so callers need not branch on the backend.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load_aligned(const float* p) noexcept {
    return load(p);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load_splat(const float* p) noexcept {
    return make_reg(*p, *p, *p, *p);
}

MATHEMATICS_INLINE void MATHEMATICS_CALL store(float* p, vec_reg a) noexcept {
    p[0] = lane(a, 0);
    p[1] = lane(a, 1);
    p[2] = lane(a, 2);
    p[3] = lane(a, 3);
}

MATHEMATICS_INLINE void MATHEMATICS_CALL store_aligned(float* p, vec_reg a) noexcept {
    store(p, a);
}

// ------------------------------------------------------------------- arithmetic
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
add(vec_reg a, vec_reg b) noexcept { return consteval_ops::add(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
sub(vec_reg a, vec_reg b) noexcept { return consteval_ops::sub(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul(vec_reg a, vec_reg b) noexcept { return consteval_ops::mul(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
div(vec_reg a, vec_reg b) noexcept { return consteval_ops::div(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
negate(vec_reg a) noexcept { return consteval_ops::negate(a); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
abs(vec_reg a) noexcept { return consteval_ops::abs(a); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return consteval_ops::mul_add(a, b, c);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul_sub(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return consteval_ops::mul_sub(a, b, c);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
neg_mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return consteval_ops::neg_mul_add(a, b, c);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
min(vec_reg a, vec_reg b) noexcept { return consteval_ops::min(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
max(vec_reg a, vec_reg b) noexcept { return consteval_ops::max(a, b); }

// ----------------------------------------------------------- roots, reciprocals
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL sqrt(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::sqrt(a); }
    return make_reg(std::sqrt(lane(a, 0)), std::sqrt(lane(a, 1)),
                   std::sqrt(lane(a, 2)), std::sqrt(lane(a, 3)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL rsqrt(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::rsqrt(a); }
    return make_reg(1.0f / std::sqrt(lane(a, 0)), 1.0f / std::sqrt(lane(a, 1)),
                   1.0f / std::sqrt(lane(a, 2)), 1.0f / std::sqrt(lane(a, 3)));
}

// No approximate reciprocal instruction to reach for, so the Est forms are exact
// here. Callers get a correct answer either way; only the cost differs by target.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL rsqrt_est(vec_reg a) noexcept {
    return rsqrt(a);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL recip(vec_reg a) noexcept {
    return consteval_ops::recip(a);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL recip_est(vec_reg a) noexcept {
    return recip(a);
}

// ---------------------------------------------------------------------- bitwise
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_and(vec_reg a, vec_reg b) noexcept { return consteval_ops::bit_and(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_and_not(vec_reg a, vec_reg b) noexcept { return consteval_ops::bit_and_not(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_or(vec_reg a, vec_reg b) noexcept { return consteval_ops::bit_or(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_xor(vec_reg a, vec_reg b) noexcept { return consteval_ops::bit_xor(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_not(vec_reg a) noexcept { return consteval_ops::bit_not(a); }

// ------------------------------------------------------------------- comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_eq(vec_reg a, vec_reg b) noexcept { return consteval_ops::cmp_eq(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_ne(vec_reg a, vec_reg b) noexcept { return consteval_ops::cmp_ne(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_lt(vec_reg a, vec_reg b) noexcept { return consteval_ops::cmp_lt(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_le(vec_reg a, vec_reg b) noexcept { return consteval_ops::cmp_le(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_gt(vec_reg a, vec_reg b) noexcept { return consteval_ops::cmp_gt(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_ge(vec_reg a, vec_reg b) noexcept { return consteval_ops::cmp_ge(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
select(mask_reg mask, vec_reg if_true, vec_reg if_false) noexcept {
    return consteval_ops::select(mask, if_true, if_false);
}

// ------------------------------------------------------------------- predicates
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr int MATHEMATICS_CALL
move_mask(vec_reg a) noexcept { return consteval_ops::move_mask(a); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool MATHEMATICS_CALL
all_true(mask_reg m) noexcept { return consteval_ops::all_true(m); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool MATHEMATICS_CALL
any_true(mask_reg m) noexcept { return consteval_ops::any_true(m); }

// ------------------------------------------------------------------- reductions
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot2(vec_reg a, vec_reg b) noexcept { return consteval_ops::dot2(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot3(vec_reg a, vec_reg b) noexcept { return consteval_ops::dot3(a, b); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot4(vec_reg a, vec_reg b) noexcept { return consteval_ops::dot4(a, b); }

// ---------------------------------------------------------------------- shuffle
template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL shuffle(vec_reg a) noexcept {
    return consteval_ops::shuffle<x, y, z, w>(a);
}

template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
shuffle(vec_reg a, vec_reg b) noexcept {
    return consteval_ops::shuffle<x, y, z, w>(a, b);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
splat_x(vec_reg a) noexcept { return consteval_ops::splat_x(a); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
splat_y(vec_reg a) noexcept { return consteval_ops::splat_y(a); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
splat_z(vec_reg a) noexcept { return consteval_ops::splat_z(a); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
splat_w(vec_reg a) noexcept { return consteval_ops::splat_w(a); }

// ------------------------------------------------------------------ lane readout
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL
get_x(vec_reg a) noexcept { return lane(a, 0); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL
get_y(vec_reg a) noexcept { return lane(a, 1); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL
get_z(vec_reg a) noexcept { return lane(a, 2); }

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL
get_w(vec_reg a) noexcept { return lane(a, 3); }

} // namespace math

#endif // MATHEMATICS_ARCH_SIMD_SCALAR_HPP
