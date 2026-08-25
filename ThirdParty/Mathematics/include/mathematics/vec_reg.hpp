// mathematics/vec_reg.hpp — the register-level API.
//
// vec_reg wraps one 128-bit SIMD register and is ALWAYS passed by value. It is
// not a storage type: Phase 2's vector2/3/4 own memory layout, and values promote
// to vec_reg to compute. docs/SPIKE-RESULTS.md §3 measures why that separation is
// mandatory rather than stylistic -- a four-float struct is decomposed across
// four registers by __vectorcall and rebuilt at every call boundary.
//
// Every operation works at compile time and at run time, selecting between a
// scalar definition and the target's instructions. The Phase 0 spike verified
// the wrapper is free: runtime output is instruction-identical to DirectXMath.
//
//   Construction   set splat zero
//   Memory         load load_aligned store store_aligned
//   Arithmetic     add sub mul div negate abs mul_add mul_sub neg_mul_add min max
//   Roots          sqrt rsqrt rsqrt_est recip recip_est
//   Bitwise        bit_and bit_and_not bit_or bit_xor bit_not
//   Comparison     cmp_eq cmp_ne cmp_lt cmp_le cmp_gt cmp_ge select
//   Predicates     move_mask all_true any_true
//   Reductions     dot2 dot3 dot4
//   Shuffle        shuffle<x, y, z, w> splat_x splat_y splat_z splat_w
//   Lanes          lane set_lane get_x get_y get_z get_w
//
// Comparisons return masks whose lanes are all-ones or all-zeros, the same
// convention DirectXMath uses; select consumes them. Because an all-ones lane
// reads as a NaN, mask values must only be combined bitwise, never used in
// arithmetic.
//
// Two behaviours are target-specific and deliberately not normalised:
//   - min/max with a NaN operand (see the note in arch/simd_neon.hpp)
//   - rsqrt_est/recip_est precision, which is approximate on SSE and NEON and
//     exact on the scalar fallback
#ifndef MATHEMATICS_VEC_REG_HPP
#define MATHEMATICS_VEC_REG_HPP

#include <mathematics/arch/simd_select.hpp>

#endif // MATHEMATICS_VEC_REG_HPP
