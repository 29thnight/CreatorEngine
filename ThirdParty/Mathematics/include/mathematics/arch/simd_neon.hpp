// mathematics/arch/simd_neon.hpp — AArch64 NEON backend.
//
// Verified by CI on AArch64 Linux (GCC and Clang), which is also the only place
// it is exercised -- no ARM toolchain is available locally. The MSVC/ARM64 path
// through __n128 remains unverified: no CI leg builds it. Correctness is
// favoured over instruction count throughout; the places worth tuning are
// marked.
//
// Targets AArch64, which is what _M_ARM64, _M_ARM64EC and __aarch64__ select.
// Several intrinsics used here (vdivq_f32, vsqrtq_f32, vaddvq_f32) do not exist
// on 32-bit ARM, which is out of scope -- config.hpp routes it to the scalar
// backend instead.
#ifndef MATHEMATICS_ARCH_SIMD_NEON_HPP
#define MATHEMATICS_ARCH_SIMD_NEON_HPP

#include <mathematics/arch/consteval_ops.hpp>

#if !MATHEMATICS_SIMD_NEON
#  error "simd_neon.hpp included without the NEON backend selected"
#endif

namespace math {

namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE uint32x4_t as_bits(vec_reg a) noexcept {
    return vreinterpretq_u32_f32(a.v);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg from_bits_reg(uint32x4_t b) noexcept {
    return vec_reg{vreinterpretq_f32_u32(b)};
}

} // namespace detail

// ---------------------------------------------------------------- construction
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
set(float x, float y, float z, float w) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::set(x, y, z, w); }
    // Loaded from a local rather than brace-initializing float32x4_t: the
    // vector-literal form is a compiler extension that GCC and Clang do not
    // spell identically, while vld1q_f32 is plain ACLE.
    const float lanes[4] = {x, y, z, w};
    return vec_reg{vld1q_f32(lanes)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat(float s) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::splat(s); }
    return vec_reg{vdupq_n_f32(s)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL zero() noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::zero(); }
    return vec_reg{vdupq_n_f32(0.0f)};
}

// ------------------------------------------------------------------ load, store
// NEON loads tolerate unaligned addresses, so the Aligned variants carry no
// extra instruction here -- they exist for interface parity with SSE.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load(const float* p) noexcept {
    return vec_reg{vld1q_f32(p)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load_aligned(const float* p) noexcept {
    return vec_reg{vld1q_f32(p)};
}

// load-and-broadcast in one instruction. See the note on the SSE version: it
// exists so a matrix multiply does not spend sixteen operations on the shuffle
// unit when the values it needs are already addressable in memory.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load_splat(const float* p) noexcept {
    return vec_reg{vld1q_dup_f32(p)};
}

MATHEMATICS_INLINE void MATHEMATICS_CALL store(float* p, vec_reg a) noexcept {
    vst1q_f32(p, a.v);
}

MATHEMATICS_INLINE void MATHEMATICS_CALL store_aligned(float* p, vec_reg a) noexcept {
    vst1q_f32(p, a.v);
}

// ------------------------------------------------------------------- arithmetic
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
add(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::add(a, b); }
    return vec_reg{vaddq_f32(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
sub(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::sub(a, b); }
    return vec_reg{vsubq_f32(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::mul(a, b); }
    return vec_reg{vmulq_f32(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
div(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::div(a, b); }
    return vec_reg{vdivq_f32(a.v, b.v)};
}

// XOR of the sign bit, not vnegq_f32: negating -0.0f must give +0.0f, which is
// what consteval_ops and the SSE path both do.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL negate(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::negate(a); }
    return detail::from_bits_reg(veorq_u32(detail::as_bits(a), vdupq_n_u32(sign_bit)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL abs(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::abs(a); }
    return detail::from_bits_reg(vandq_u32(detail::as_bits(a), vdupq_n_u32(abs_mask)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::mul_add(a, b, c); }
    return vec_reg{vfmaq_f32(c.v, a.v, b.v)};   // c + a*b
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul_sub(vec_reg a, vec_reg b, vec_reg c) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::mul_sub(a, b, c); }
    // -c + a*b, computed directly. The tempting spelling -(c - a*b) via
    // vnegq_f32(vfmsq_f32(...)) gives the wrong zero sign when a*b == c exactly:
    // the subtraction yields +0.0 and the negate then flips it to -0.0, while
    // every other backend and consteval_ops produce +0.0.
    return vec_reg{vfmaq_f32(vnegq_f32(c.v), a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
neg_mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::neg_mul_add(a, b, c); }
    return vec_reg{vfmsq_f32(c.v, a.v, b.v)};   // c - a*b
}

// KNOWN DIVERGENCES from the SSE backend and from consteval_ops, in two
// distinct cases. DirectXMath carries the same split and leaves both
// unspecified; matching x86 would cost a compare and a select on every call, for
// behaviour no caller should be relying on. Tests assert both explicitly per
// target rather than excluding them.
//
//   NaN    ARM FMIN/FMAX return a quiet NaN when either operand is NaN.
//          minps/maxps return the second operand. So Min(NaN, x) is NaN here
//          and x on x86.
//
//   ±0.0   ARM FMIN/FMAX treat -0.0 as strictly less than +0.0 regardless of
//          operand order. minps/maxps compare them equal and fall through to
//          the second operand. So Min(-0.0, +0.0) is -0.0 here and +0.0 on x86.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
min(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::min(a, b); }
    return vec_reg{vminq_f32(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
max(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::max(a, b); }
    return vec_reg{vmaxq_f32(a.v, b.v)};
}

// ----------------------------------------------------------- roots, reciprocals
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL sqrt(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::sqrt(a); }
    return vec_reg{vsqrtq_f32(a.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL rsqrt(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::rsqrt(a); }
    return vec_reg{vdivq_f32(vdupq_n_f32(1.0f), vsqrtq_f32(a.v))};
}

// One Newton-Raphson step on top of the estimate. ARM's vrsqrteq_f32 alone is
// specified to roughly 8 bits, well short of the ~12 that SSE's rsqrtps gives,
// and the refinement step is what the instruction is designed to be paired with.
// Still far cheaper than the exact form, and it keeps Est meaning the same thing
// to callers on both targets.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL rsqrt_est(vec_reg a) noexcept {
    const float32x4_t e = vrsqrteq_f32(a.v);
    return vec_reg{vmulq_f32(e, vrsqrtsq_f32(vmulq_f32(a.v, e), e))};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL recip(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::recip(a); }
    return vec_reg{vdivq_f32(vdupq_n_f32(1.0f), a.v)};
}

// Refined for the same reason as rsqrt_est above.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL recip_est(vec_reg a) noexcept {
    const float32x4_t e = vrecpeq_f32(a.v);
    return vec_reg{vmulq_f32(e, vrecpsq_f32(a.v, e))};
}

// ---------------------------------------------------------------------- bitwise
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_and(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_and(a, b); }
    return detail::from_bits_reg(vandq_u32(detail::as_bits(a), detail::as_bits(b)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_and_not(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_and_not(a, b); }
    // vbicq_u32(x, y) is x & ~y, so the operands swap to produce ~a & b.
    return detail::from_bits_reg(vbicq_u32(detail::as_bits(b), detail::as_bits(a)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_or(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_or(a, b); }
    return detail::from_bits_reg(vorrq_u32(detail::as_bits(a), detail::as_bits(b)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_xor(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_xor(a, b); }
    return detail::from_bits_reg(veorq_u32(detail::as_bits(a), detail::as_bits(b)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL bit_not(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_not(a); }
    return detail::from_bits_reg(vmvnq_u32(detail::as_bits(a)));
}

// ------------------------------------------------------------------- comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_eq(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_eq(a, b); }
    return detail::from_bits_reg(vceqq_f32(a.v, b.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_ne(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_ne(a, b); }
    return detail::from_bits_reg(vmvnq_u32(vceqq_f32(a.v, b.v)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_lt(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_lt(a, b); }
    return detail::from_bits_reg(vcltq_f32(a.v, b.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_le(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_le(a, b); }
    return detail::from_bits_reg(vcleq_f32(a.v, b.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_gt(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_gt(a, b); }
    return detail::from_bits_reg(vcgtq_f32(a.v, b.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_ge(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_ge(a, b); }
    return detail::from_bits_reg(vcgeq_f32(a.v, b.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
select(mask_reg mask, vec_reg if_true, vec_reg if_false) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::select(mask, if_true, if_false); }
    return vec_reg{vbslq_f32(detail::as_bits(mask), if_true.v, if_false.v)};
}

// ------------------------------------------------------------------- predicates
// NEON has no movmskps. Each lane's sign bit is isolated, shifted into its own
// bit position, and the four are summed.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr int MATHEMATICS_CALL move_mask(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::move_mask(a); }
    const uint32x4_t signs = vshrq_n_u32(detail::as_bits(a), 31);
    const int32_t positions[4] = {0, 1, 2, 3};
    return static_cast<int>(vaddvq_u32(vshlq_u32(signs, vld1q_s32(positions))));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool MATHEMATICS_CALL all_true(mask_reg m) noexcept {
    return move_mask(m) == 0xF;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool MATHEMATICS_CALL any_true(mask_reg m) noexcept {
    return move_mask(m) != 0;
}

// ------------------------------------------------------------------- reductions
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot2(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::dot2(a, b); }
    const float32x4_t m = vmulq_f32(a.v, b.v);
    return vec_reg{vdupq_n_f32(vgetq_lane_f32(m, 0) + vgetq_lane_f32(m, 1))};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot3(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::dot3(a, b); }
    const float32x4_t m = vsetq_lane_f32(0.0f, vmulq_f32(a.v, b.v), 3);
    return vec_reg{vdupq_n_f32(vaddvq_f32(m))};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot4(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::dot4(a, b); }
    return vec_reg{vdupq_n_f32(vaddvq_f32(vmulq_f32(a.v, b.v)))};
}

// ---------------------------------------------------------------------- shuffle
// NEON has no general single-instruction permute, so an arbitrary shuffle is
// built lane by lane. The template indices make each vgetq/vsetq index a
// constant, which the intrinsics require. Splats below use vdupq_laneq_f32,
// which is a single instruction and covers the common cases; a wider table of
// specialisations for patterns the matrix code leans on belongs to Phase 3, once
// there is a benchmark showing which ones matter.
template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL shuffle(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::shuffle<x, y, z, w>(a); }
    // Seeded from a.v rather than a zero vector: all four lanes are overwritten
    // below, so materializing a zero first is pure waste.
    float32x4_t r = a.v;
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, x), r, 0);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, y), r, 1);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, z), r, 2);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, w), r, 3);
    return vec_reg{r};
}

template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
shuffle(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::shuffle<x, y, z, w>(a, b); }
    // Seeded from a.v rather than a zero vector: all four lanes are overwritten
    // below, so materializing a zero first is pure waste.
    float32x4_t r = a.v;
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, x), r, 0);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, y), r, 1);
    r = vsetq_lane_f32(vgetq_lane_f32(b.v, z), r, 2);
    r = vsetq_lane_f32(vgetq_lane_f32(b.v, w), r, 3);
    return vec_reg{r};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_x(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::splat_x(a); }
    return vec_reg{vdupq_laneq_f32(a.v, 0)};
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_y(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::splat_y(a); }
    return vec_reg{vdupq_laneq_f32(a.v, 1)};
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_z(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::splat_z(a); }
    return vec_reg{vdupq_laneq_f32(a.v, 2)};
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_w(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::splat_w(a); }
    return vec_reg{vdupq_laneq_f32(a.v, 3)};
}

// ------------------------------------------------------------------ lane readout
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_x(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 0); }
    return vgetq_lane_f32(a.v, 0);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_y(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 1); }
    return vgetq_lane_f32(a.v, 1);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_z(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 2); }
    return vgetq_lane_f32(a.v, 2);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_w(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 3); }
    return vgetq_lane_f32(a.v, 3);
}

} // namespace math

#endif // MATHEMATICS_ARCH_SIMD_NEON_HPP
