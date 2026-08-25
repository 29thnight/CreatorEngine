// mathematics/arch/reg.hpp — the register type and the primitives every backend needs.
//
// Layer 1 of the backend stack:
//   reg.hpp            <- this file: vec_reg, lane access, bit helpers
//   consteval_ops.hpp  <- scalar semantics, the single definition of what each
//                         operation means
//   simd_{sse,neon,scalar}.hpp <- runtime instruction selection per target
//   simd_select.hpp    <- includes exactly one backend
#ifndef MATHEMATICS_ARCH_REG_HPP
#define MATHEMATICS_ARCH_REG_HPP

#include <mathematics/config.hpp>

#include <bit>
#include <cstdint>
#include <limits>

#if MATHEMATICS_SIMD_SSE
#  include <immintrin.h>
#elif MATHEMATICS_SIMD_NEON
#  include <arm_neon.h>
#endif

namespace math {

// The bit-level implementation below is intentionally binary32-specific. Fail
// at the include site on an exotic target instead of silently interpreting a
// different floating-point representation as IEEE-754 sign/exponent bits.
static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

// ---------------------------------------------------------------- register type
#if MATHEMATICS_SIMD_SSE
using reg_native = __m128;
#elif MATHEMATICS_SIMD_NEON
using reg_native = float32x4_t;
#else
struct reg_native { float f[4]; };
#endif

// One 128-bit SIMD register: four floats, always passed by value, never used as
// a storage type. Phase 2's vector2/3/4 own memory layout; vec_reg owns computation.
struct vec_reg {
    reg_native v;
};

// Masks are vec_reg too, with each lane either all-ones or all-zeros -- the same
// convention DirectXMath uses for XMVECTOR masks. Comparisons produce them and
// select consumes them. An all-ones lane read as a float is a NaN, so mask lanes
// must only ever be moved and combined bitwise, never used in arithmetic.
using mask_reg = vec_reg;

// ------------------------------------------------------------------ bit helpers
inline constexpr std::uint32_t lane_true = 0xFFFFFFFFu;
inline constexpr std::uint32_t lane_false = 0x00000000u;
inline constexpr std::uint32_t sign_bit = 0x80000000u;
inline constexpr std::uint32_t abs_mask = 0x7FFFFFFFu;

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::uint32_t bits_of(float x) noexcept {
    return std::bit_cast<std::uint32_t>(x);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float from_bits(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

// ------------------------------------------------------------------ lane access
// Lane layout is NOT portable. On genuine MSVC the intrinsic vector types are
// magic unions (__m128 with m128_f32[], __n128 with n128_f32[]); on Clang and GCC
// they are native vector types that subscript directly. Every constant-evaluated
// read or write must route through these two helpers.
//
// Deliberately not written with vst1q_f32/_mm_store_ps, which would be portable
// but are not constant-evaluable -- and serving the compile-time path is the
// entire reason these exist.
//
// The __n128 branch (MSVC targeting ARM64) is still unverified: no CI leg builds
// that combination, and it rests on MSVC's documented layout. Every other branch
// here is covered.
// The four lanes as a plain array. Used only to reinterpret a register during
// constant evaluation on Clang and GCC, where subscripting a native vector type
// is not a constant expression -- Clang rejects it outright with
// -Winvalid-constexpr. std::bit_cast is constant-evaluable for those compilers
// because their vector types contain no union members; on MSVC the intrinsic
// type IS a union, which rules bit_cast out but makes the member accessible
// directly instead. The two restrictions are exactly complementary.
struct reg_lanes {
    float f[4];
};

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float lane(const vec_reg& r, int i) noexcept {
#if MATHEMATICS_MSVC_INTRINSIC_UNION && MATHEMATICS_SIMD_SSE
    return r.v.m128_f32[i];
#elif MATHEMATICS_MSVC_INTRINSIC_UNION && MATHEMATICS_SIMD_NEON
    return r.v.n128_f32[i];
#elif MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_NEON
    MATHEMATICS_IF_CONSTEVAL { return std::bit_cast<reg_lanes>(r.v).f[i]; }
    return r.v[i];
#else
    return r.v.f[i];
#endif
}

namespace detail {

// Writes a lane in place. Valid during constant evaluation only on MSVC, where
// the intrinsic types are unions: GCC -- and some Clang versions -- reject
// assignment to an element of a native vector type in a constant expression.
// The portable compile-time path is make_reg, which builds the whole register at
// once; this helper exists for make_reg's own MSVC branch and for runtime writes.
MATHEMATICS_INLINE constexpr void write_lane(reg_native& v, int i, float x) noexcept {
#if MATHEMATICS_MSVC_INTRINSIC_UNION && MATHEMATICS_SIMD_SSE
    v.m128_f32[i] = x;
#elif MATHEMATICS_MSVC_INTRINSIC_UNION && MATHEMATICS_SIMD_NEON
    v.n128_f32[i] = x;
#elif MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_NEON
    // Same split as Lane: assigning an element of a native vector type is not a
    // constant expression, so the compile-time path goes through the lane array.
    MATHEMATICS_IF_CONSTEVAL {
        reg_lanes lanes = std::bit_cast<reg_lanes>(v);
        lanes.f[i] = x;
        v = std::bit_cast<reg_native>(lanes);
        return;
    }
    v[i] = x;
#else
    v.f[i] = x;
#endif
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::uint32_t
lane_bits(const vec_reg& r, int i) noexcept {
    return bits_of(lane(r, i));
}

// ----------------------------------------------------------------- construction
// The one way to build a register from four scalars.
//
// The runtime and compile-time paths are split even though the compile-time one
// would compile at runtime too, because on MSVC it produces a value the
// optimizer then handles badly. Writing lanes -- or aggregate-initializing
// `__m128{x, y, z, w}`, which is the same thing through the union's float[4]
// member -- leaves MSVC treating the object as four scalars for the rest of its
// lifetime, emitting every later access element-wise. A value built that way and
// carried into a loop gets rebuilt with vinsertps on entry and torn apart with
// vshufps on exit, around a single arithmetic instruction. Measured on a mul_add
// chain: 1.9x slower than DirectXMath, with the arithmetic itself identical.
//
// The intrinsics keep the value vector-typed, and constant evaluation never sees
// them.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
make_reg(float x, float y, float z, float w) noexcept {
    MATHEMATICS_IF_CONSTEVAL {
        // The compile-time form is per-compiler because the register type is:
        // MSVC's intrinsic types are unions whose float member can be written
        // lane by lane, while Clang's and GCC's are native vector types that
        // must be initialized whole -- GCC rejects element assignment on one
        // during constant evaluation outright, and Clang does depending on
        // version. Both were caught by CI, having passed locally.
#if MATHEMATICS_MSVC_INTRINSIC_UNION
        vec_reg r{};
        detail::write_lane(r.v, 0, x);
        detail::write_lane(r.v, 1, y);
        detail::write_lane(r.v, 2, z);
        detail::write_lane(r.v, 3, w);
        return r;
#elif MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_NEON
        // bit_cast rather than brace-initializing the vector type: the literal
        // form is a compiler extension that GCC and Clang do not spell alike,
        // while bit_cast is standard and constant-evaluable for both.
        return vec_reg{std::bit_cast<reg_native>(reg_lanes{{x, y, z, w}})};
#else
        return vec_reg{reg_native{{x, y, z, w}}};
#endif
    }
#if MATHEMATICS_SIMD_SSE
    return vec_reg{_mm_setr_ps(x, y, z, w)};   // setr takes lanes in order
#elif MATHEMATICS_SIMD_NEON
    // Loaded rather than brace-initializing float32x4_t: vld1q_f32 is plain
    // ACLE, where the vector-literal form is a compiler extension.
    const float lanes[4] = {x, y, z, w};
    return vec_reg{vld1q_f32(lanes)};
#else
    return vec_reg{reg_native{{x, y, z, w}}};
#endif
}

// Writing a single lane during constant evaluation goes through make_reg, which
// is the only portable way to build a register (see the note above). At runtime
// the direct write is used.
MATHEMATICS_INLINE constexpr void set_lane(vec_reg& r, int i, float x) noexcept {
    MATHEMATICS_IF_CONSTEVAL {
        r = make_reg(i == 0 ? x : lane(r, 0), i == 1 ? x : lane(r, 1),
                    i == 2 ? x : lane(r, 2), i == 3 ? x : lane(r, 3));
        return;
    }
    detail::write_lane(r.v, i, x);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
make_mask_reg(std::uint32_t x, std::uint32_t y,
            std::uint32_t z, std::uint32_t w) noexcept {
    return make_reg(from_bits(x), from_bits(y), from_bits(z), from_bits(w));
}

} // namespace math

#endif // MATHEMATICS_ARCH_REG_HPP
