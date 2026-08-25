// mathematics/arch/simd_sse.hpp — x86 SSE/AVX backend.
//
// Every function has the same two-part shape: the compile-time branch defers to
// consteval_ops, which defines what the operation means, and the runtime branch
// picks instructions. Nothing here may redefine semantics -- if a result differs
// from consteval_ops, that is a bug in this file, and the parity tests exist to
// catch it.
//
// SSE2 is the floor. SSE4.1 (dpps, blendv) and FMA are used when available;
// MATHEMATICS_HAS_SSE4 and MATHEMATICS_HAS_FMA say which, and every such path has an SSE2
// fallback below it.
#ifndef MATHEMATICS_ARCH_SIMD_SSE_HPP
#define MATHEMATICS_ARCH_SIMD_SSE_HPP

#include <mathematics/arch/consteval_ops.hpp>

#include <cstring>

#if !MATHEMATICS_SIMD_SSE
#  error "simd_sse.hpp included without the SSE backend selected"
#endif

namespace math {

namespace detail {

// _mm_shuffle_ps takes its selectors in reverse lane order, and its two-source
// form draws lanes 0-1 from the first operand and 2-3 from the second. Wrapping
// it in lane order removes a persistent source of transposed-argument bugs.
template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE __m128 shuffle_raw(__m128 a, __m128 b) noexcept {
    return _mm_shuffle_ps(a, b, _MM_SHUFFLE(w, z, y, x));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE __m128 sign_mask() noexcept {
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(sign_bit)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE __m128 abs_mask_reg() noexcept {
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(abs_mask)));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE __m128 all_ones() noexcept {
    return _mm_castsi128_ps(_mm_set1_epi32(-1));
}

// Horizontal sum broadcast to every lane, in two shuffle/add pairs. Used only
// where dpps is unavailable.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE __m128 h_sum_all(__m128 m) noexcept {
    const __m128 t = _mm_add_ps(m, shuffle_raw<1, 0, 3, 2>(m, m));
    return _mm_add_ps(t, shuffle_raw<2, 3, 0, 1>(t, t));
}

} // namespace detail

// ---------------------------------------------------------------- construction
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
set(float x, float y, float z, float w) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::set(x, y, z, w); }
    return vec_reg{_mm_set_ps(w, z, y, x)};   // takes lanes in reverse order
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat(float s) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::splat(s); }
    return vec_reg{_mm_set1_ps(s)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL zero() noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::zero(); }
    return vec_reg{_mm_setzero_ps()};
}

// ------------------------------------------------------------------ load, store
// load/store take unaligned pointers; the aligned forms require 16-byte
// alignment and fault otherwise.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load(const float* p) noexcept {
    return vec_reg{_mm_loadu_ps(p)};
}

// Twelve-byte-safe packed object load. The caller passes the complete object's
// address, not &first_member: byte access may span an object's representation,
// while float-pointer arithmetic across distinct members would be undefined.
// The 8+4-byte split matches vector3's stores and enables forwarding in chains.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load3(const void* object) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(object);
    std::uint64_t xy_bits;
    std::uint32_t z_bits;
    std::memcpy(&xy_bits, bytes, sizeof(xy_bits));
    std::memcpy(&z_bits, bytes + sizeof(xy_bits), sizeof(z_bits));
    const __m128 xy = _mm_castsi128_ps(
        _mm_cvtsi64_si128(std::bit_cast<std::int64_t>(xy_bits)));
    const __m128 z = _mm_castsi128_ps(
        _mm_cvtsi32_si128(std::bit_cast<std::int32_t>(z_bits)));
#if MATHEMATICS_HAS_SSE4
    return vec_reg{_mm_insert_ps(xy, z, 0x20)};
#else
    return vec_reg{_mm_movelh_ps(xy, z)};
#endif
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load_aligned(const float* p) noexcept {
    return vec_reg{_mm_load_ps(p)};
}

// Reads one float and broadcasts it, in a single instruction where AVX is
// available. This is not the same as load followed by splat_x: the shuffle form
// occupies the shuffle port, and a matrix multiply issues sixteen of them, which
// makes that port -- not the arithmetic -- the bottleneck. Broadcasting straight
// from memory moves the work to the load port and leaves the shuffle port free.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL load_splat(const float* p) noexcept {
#if MATHEMATICS_HAS_AVX
    return vec_reg{_mm_broadcast_ss(p)};
#else
    return vec_reg{_mm_load_ps1(p)};
#endif
}

MATHEMATICS_INLINE void MATHEMATICS_CALL store(float* p, vec_reg a) noexcept {
    _mm_storeu_ps(p, a.v);
}

MATHEMATICS_INLINE void MATHEMATICS_CALL store_aligned(float* p, vec_reg a) noexcept {
    _mm_store_ps(p, a.v);
}

// The mirror of load3, and the same 8+4 split for the same reason. Writing the
// three lanes as three scalars instead costs two extra shuffles per store,
// because x is the only lane a scalar store can reach without one. MSVC does
// exactly that when a vector3 is built lane by lane, and on a machine whose
// shuffle ports are the bottleneck -- cross issues four of them already -- those
// two are a quarter of the loop. Clang folds the lane-wise form into this shape
// on its own; MSVC does not, and cannot be talked into it from the value side.
MATHEMATICS_INLINE void MATHEMATICS_CALL store3(void* object, vec_reg a) noexcept {
    auto* const bytes = static_cast<unsigned char*>(object);
    // Spelling matters here, and not for the reason it usually does.
    // _mm_storel_pi is the domain-correct movlps and MSVC will not fold it into
    // the caller's destination -- it routes the object through a stack slot and
    // reloads it, which is two memory operations more than this exists to save.
    // _mm_store_sd is worse still: GCC's emmintrin.h implements it as a plain
    // store through double*, which these three floats are not. __m128i* is a
    // may-alias type in every backend that defines it, and z reaches a float
    // member, so neither write needs a memcpy to stay clear of TBAA.
    _mm_storel_epi64(reinterpret_cast<__m128i*>(bytes), _mm_castps_si128(a.v));
#if MATHEMATICS_HAS_SSE4
    const int z_bits = _mm_extract_ps(a.v, 2);
    std::memcpy(bytes + 8, &z_bits, sizeof(z_bits));
#else
    _mm_store_ss(reinterpret_cast<float*>(bytes + 8),
                 _mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(2, 2, 2, 2)));
#endif
}

// ------------------------------------------------------------------- arithmetic
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
add(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::add(a, b); }
    return vec_reg{_mm_add_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
sub(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::sub(a, b); }
    return vec_reg{_mm_sub_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::mul(a, b); }
    return vec_reg{_mm_mul_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
div(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::div(a, b); }
    return vec_reg{_mm_div_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL negate(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::negate(a); }
    return vec_reg{_mm_xor_ps(a.v, detail::sign_mask())};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL abs(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::abs(a); }
    return vec_reg{_mm_and_ps(a.v, detail::abs_mask_reg())};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::mul_add(a, b, c); }
#if MATHEMATICS_HAS_FMA
    return vec_reg{_mm_fmadd_ps(a.v, b.v, c.v)};
#else
    return vec_reg{_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)};
#endif
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
mul_sub(vec_reg a, vec_reg b, vec_reg c) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::mul_sub(a, b, c); }
#if MATHEMATICS_HAS_FMA
    return vec_reg{_mm_fmsub_ps(a.v, b.v, c.v)};
#else
    return vec_reg{_mm_sub_ps(_mm_mul_ps(a.v, b.v), c.v)};
#endif
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
neg_mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::neg_mul_add(a, b, c); }
#if MATHEMATICS_HAS_FMA
    return vec_reg{_mm_fnmadd_ps(a.v, b.v, c.v)};   // c - a*b
#else
    return vec_reg{_mm_sub_ps(c.v, _mm_mul_ps(a.v, b.v))};
#endif
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
min(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::min(a, b); }
    return vec_reg{_mm_min_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
max(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::max(a, b); }
    return vec_reg{_mm_max_ps(a.v, b.v)};
}

// ----------------------------------------------------------- roots, reciprocals
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL sqrt(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::sqrt(a); }
    return vec_reg{_mm_sqrt_ps(a.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL rsqrt(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::rsqrt(a); }
    return vec_reg{_mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(a.v))};
}

// ~12-bit approximation from rsqrtps. Roughly an order of magnitude cheaper than
// the exact form and accurate enough for normalizing render-time vectors, which
// is why DirectXMath exposes the same Est distinction.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL rsqrt_est(vec_reg a) noexcept {
    return vec_reg{_mm_rsqrt_ps(a.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL recip(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::recip(a); }
    return vec_reg{_mm_div_ps(_mm_set1_ps(1.0f), a.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg MATHEMATICS_CALL recip_est(vec_reg a) noexcept {
    return vec_reg{_mm_rcp_ps(a.v)};
}

// ---------------------------------------------------------------------- bitwise
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_and(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_and(a, b); }
    return vec_reg{_mm_and_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_and_not(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_and_not(a, b); }
    return vec_reg{_mm_andnot_ps(a.v, b.v)};   // ~a & b
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_or(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_or(a, b); }
    return vec_reg{_mm_or_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
bit_xor(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_xor(a, b); }
    return vec_reg{_mm_xor_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL bit_not(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::bit_not(a); }
    return vec_reg{_mm_xor_ps(a.v, detail::all_ones())};
}

// ------------------------------------------------------------------- comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_eq(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_eq(a, b); }
    return vec_reg{_mm_cmpeq_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_ne(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_ne(a, b); }
    return vec_reg{_mm_cmpneq_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_lt(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_lt(a, b); }
    return vec_reg{_mm_cmplt_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_le(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_le(a, b); }
    return vec_reg{_mm_cmple_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_gt(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_gt(a, b); }
    return vec_reg{_mm_cmpgt_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr mask_reg MATHEMATICS_CALL
cmp_ge(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::cmp_ge(a, b); }
    return vec_reg{_mm_cmpge_ps(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
select(mask_reg mask, vec_reg if_true, vec_reg if_false) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::select(mask, if_true, if_false); }
#if MATHEMATICS_HAS_SSE4
    // blendv selects per lane on the sign bit alone. For the all-ones/all-zeros
    // masks comparisons produce it agrees with the bitwise blend below; for a
    // hand-built partial mask it does not, which is why masks are documented as
    // all-ones or all-zeros in reg.hpp.
    return vec_reg{_mm_blendv_ps(if_false.v, if_true.v, mask.v)};
#else
    return vec_reg{_mm_or_ps(_mm_and_ps(mask.v, if_true.v),
                            _mm_andnot_ps(mask.v, if_false.v))};
#endif
}

// ------------------------------------------------------------------- predicates
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr int MATHEMATICS_CALL move_mask(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::move_mask(a); }
    return _mm_movemask_ps(a.v);
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
#if MATHEMATICS_HAS_SSE4
    return vec_reg{_mm_dp_ps(a.v, b.v, 0x3F)};   // multiply xy, broadcast to all
#else
    const __m128 m = _mm_mul_ps(a.v, b.v);
    const __m128 xy = _mm_add_ps(m, detail::shuffle_raw<1, 1, 1, 1>(m, m));
    return vec_reg{detail::shuffle_raw<0, 0, 0, 0>(xy, xy)};
#endif
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot3(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::dot3(a, b); }
#if MATHEMATICS_HAS_SSE4
    return vec_reg{_mm_dp_ps(a.v, b.v, 0x7F)};   // multiply xyz, broadcast to all
#else
    const __m128 m = _mm_mul_ps(a.v, b.v);
    // Clear w before summing so it cannot contribute.
    const __m128 keep_xyz = _mm_castsi128_ps(_mm_setr_epi32(-1, -1, -1, 0));
    return vec_reg{detail::h_sum_all(_mm_and_ps(m, keep_xyz))};
#endif
}

// dpps in one instruction where SSE4.1 exists; without it the shuffle chain
// costs three and loses to DirectXMath (docs/SPIKE-RESULTS.md §4).
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
dot4(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::dot4(a, b); }
#if MATHEMATICS_HAS_SSE4
    return vec_reg{_mm_dp_ps(a.v, b.v, 0xFF)};
#else
    return vec_reg{detail::h_sum_all(_mm_mul_ps(a.v, b.v))};
#endif
}

// ---------------------------------------------------------------------- shuffle
template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL shuffle(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::shuffle<x, y, z, w>(a); }
    return vec_reg{detail::shuffle_raw<x, y, z, w>(a.v, a.v)};
}

template <int x, int y, int z, int w>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL
shuffle(vec_reg a, vec_reg b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return consteval_ops::shuffle<x, y, z, w>(a, b); }
    return vec_reg{detail::shuffle_raw<x, y, z, w>(a.v, b.v)};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_x(vec_reg a) noexcept {
    return shuffle<0, 0, 0, 0>(a);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_y(vec_reg a) noexcept {
    return shuffle<1, 1, 1, 1>(a);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_z(vec_reg a) noexcept {
    return shuffle<2, 2, 2, 2>(a);
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg MATHEMATICS_CALL splat_w(vec_reg a) noexcept {
    return shuffle<3, 3, 3, 3>(a);
}

// ------------------------------------------------------------------ lane readout
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_x(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 0); }
    return _mm_cvtss_f32(a.v);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_y(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 1); }
    return _mm_cvtss_f32(detail::shuffle_raw<1, 1, 1, 1>(a.v, a.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_z(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 2); }
    return _mm_cvtss_f32(detail::shuffle_raw<2, 2, 2, 2>(a.v, a.v));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float MATHEMATICS_CALL get_w(vec_reg a) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return lane(a, 3); }
    return _mm_cvtss_f32(detail::shuffle_raw<3, 3, 3, 3>(a.v, a.v));
}

} // namespace math

#endif // MATHEMATICS_ARCH_SIMD_SSE_HPP
