// mathematics/vector_common.hpp — operations shared by vector2, vector3 and vector4.
//
// The three types differ only in how many lanes take part. Everything that does
// not depend on that count is defined once here, constrained on a concept, so
// the three headers carry only their own storage, constructors, and the handful
// of genuinely dimension-specific operations (cross, and the 2D scalar cross).
//
// Every operation promotes to vec_reg, computes there, and comes back. The types
// themselves stay packed -- vector3 is twelve bytes, not a padded register --
// so they drop into vertex buffers and struct members unchanged. Force-inlining
// is what keeps a chain of operations in registers between the promotion and
// the store; docs/PLAN.md Phase 2 records the measurement that has to hold for
// that to be true.
#ifndef MATHEMATICS_VECTOR_COMMON_HPP
#define MATHEMATICS_VECTOR_COMMON_HPP

#include <mathematics/scalar.hpp>
#include <mathematics/vec_reg.hpp>

#include <cmath>
#include <concepts>

namespace math {

namespace detail {

// A packed vector type: knows how many lanes it uses and converts both ways to
// the register type.
template <typename vector_type>
concept vector_like = requires(const vector_type& v, vec_reg r) {
    { vector_type::lane_count } -> std::convertible_to<int>;
    requires vector_type::lane_count >= 2 && vector_type::lane_count <= 4;
    requires std::default_initializable<vector_type>;
    requires std::constructible_from<vector_type, float>;
    { v.x } -> std::same_as<const float&>;
    { v.y } -> std::same_as<const float&>;
    requires (vector_type::lane_count < 3 || requires { v.z; });
    requires (vector_type::lane_count < 4 || requires { v.w; });
    { v.reg() } -> std::same_as<vec_reg>;
    { vector_type::from_reg(r) } -> std::same_as<vector_type>;
};

// dot over exactly the lanes the type uses. vector3 loads w as zero, so dot4
// would give the same answer, but selecting by width keeps the emitted dpps
// immediate the same as DirectXMath's and makes the intent explicit.
template <int lane_count>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
dot_lanes(vec_reg a, vec_reg b) noexcept {
    static_assert(lane_count >= 2 && lane_count <= 4, "vectors have 2, 3 or 4 lanes");
    if constexpr (lane_count == 2) return dot2(a, b);
    else if constexpr (lane_count == 3) return dot3(a, b);
    else return dot4(a, b);
}

// Replaces the lanes a type does not own with one, for use as a divisor.
//
// vector2 and vector3 load their unused lanes as zero, so dividing one by
// another computes 0/0 there. At run time that is a NaN in a lane from_reg
// discards -- invisible. During constant evaluation it is undefined arithmetic,
// which is a hard error, and it would have made `constexpr` vector division
// impossible to write. One blend is a small price for the operation working in
// both contexts.
template <int lane_count>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg
one_in_unused_lanes(vec_reg v) noexcept {
    if constexpr (lane_count == 4) {
        return v;
    } else if constexpr (lane_count == 3) {
        return select(make_mask_reg(lane_true, lane_true, lane_true, lane_false),
                      v, splat(1.0f));
    } else {
        return select(make_mask_reg(lane_true, lane_true, lane_false, lane_false),
                      v, splat(1.0f));
    }
}

// True only for a value that can be divided by and give a meaningful answer.
//
// Written without std::isfinite, which is only constexpr from C++23 and so
// cannot serve the compile-time path on a C++20 build. `x - x == 0` is false for
// both infinities and NaN and true for every finite value, which is exactly the
// distinction wanted, and it costs one subtract.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool is_finite_non_zero(float x) noexcept {
    return x != 0.0f && x - x == 0.0f;
}

} // namespace detail

using detail::vector_like;

// ---------------------------------------------------------------- arithmetic
//
// Lane-wise arithmetic is scalar for vector2 and vector3, and SIMD only for
// vector4. That is measured, not assumed: promoting a packed three-float vector
// to a register costs a pack on the way in and three extracts on the way out,
// which is more than a single SIMD add saves over three scalar ones. A chained
// expression ran 5.25ns through the register and 3.2ns scalar; GLM, which is
// scalar for vec3, was faster than the SIMD version for exactly this reason.
//
// Reductions and geometry (dot, length, normalize, cross) still promote: there
// the work per round trip is high enough to pay for it, and normalize measured
// faster than DirectXMath's.
namespace detail {

template <vector_like vector_type, typename operation_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type componentwise(vector_type a, vector_type b, operation_type op) noexcept {
    vector_type r;
    if constexpr (vector_type::lane_count >= 2) { r.x = op(a.x, b.x); r.y = op(a.y, b.y); }
    if constexpr (vector_type::lane_count >= 3) { r.z = op(a.z, b.z); }
    if constexpr (vector_type::lane_count >= 4) { r.w = op(a.w, b.w); }
    return r;
}

} // namespace detail

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator+(vector_type a, vector_type b) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(add(a.reg(), b.reg()));
    else return detail::componentwise(a, b, [](float p, float q) noexcept { return p + q; });
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator-(vector_type a, vector_type b) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(sub(a.reg(), b.reg()));
    else return detail::componentwise(a, b, [](float p, float q) noexcept { return p - q; });
}

// Component-wise, following HLSL and GLM. The dot product is Dot, never `*`.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator*(vector_type a, vector_type b) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(mul(a.reg(), b.reg()));
    else return detail::componentwise(a, b, [](float p, float q) noexcept { return p * q; });
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator/(vector_type a, vector_type b) noexcept {
    return vector_type::from_reg(
        div(a.reg(), detail::one_in_unused_lanes<vector_type::lane_count>(b.reg())));
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator*(vector_type a, float s) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(mul(a.reg(), splat(s)));
    else return detail::componentwise(a, vector_type{s}, [](float p, float q) noexcept { return p * q; });
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator*(float s, vector_type a) noexcept {
    return a * s;
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator/(vector_type a, float s) noexcept {
    return vector_type::from_reg(div(a.reg(), splat(s)));
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type operator-(vector_type a) noexcept {
    return vector_type::from_reg(negate(a.reg()));
}

template <vector_like vector_type>
MATHEMATICS_INLINE constexpr vector_type& operator+=(vector_type& a, vector_type b) noexcept { return a = a + b; }
template <vector_like vector_type>
MATHEMATICS_INLINE constexpr vector_type& operator-=(vector_type& a, vector_type b) noexcept { return a = a - b; }
template <vector_like vector_type>
MATHEMATICS_INLINE constexpr vector_type& operator*=(vector_type& a, vector_type b) noexcept { return a = a * b; }
template <vector_like vector_type>
MATHEMATICS_INLINE constexpr vector_type& operator*=(vector_type& a, float s) noexcept { return a = a * s; }
template <vector_like vector_type>
MATHEMATICS_INLINE constexpr vector_type& operator/=(vector_type& a, vector_type b) noexcept { return a = a / b; }
template <vector_like vector_type>
MATHEMATICS_INLINE constexpr vector_type& operator/=(vector_type& a, float s) noexcept { return a = a / s; }

// ---------------------------------------------------------------- comparison
// Exact equality. Approximate comparison is near_equal, spelled out at the call
// site so nobody reaches for == and gets a tolerance they did not choose.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool operator==(vector_type a, vector_type b) noexcept {
    const mask_reg m = cmp_eq(a.reg(), b.reg());
    return (move_mask(m) & ((1 << vector_type::lane_count) - 1)) == ((1 << vector_type::lane_count) - 1);
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(vector_type a, vector_type b, float epsilon = 1e-5f) noexcept {
    const vec_reg diff = abs(sub(a.reg(), b.reg()));
    const mask_reg within = cmp_le(diff, splat(epsilon));
    return (move_mask(within) & ((1 << vector_type::lane_count) - 1)) == ((1 << vector_type::lane_count) - 1);
}

// ------------------------------------------------------------- lane-wise math
// Same width split as the operators above, and for the same measured reason:
// each of these is one SIMD instruction wrapped in a full register round trip,
// which does not pay for itself at two or three components.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type abs(vector_type a) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(abs(a.reg()));
    // Sign bit cleared rather than a comparison, so -0.0f gives +0.0f and NaN
    // payloads survive -- matching what the SIMD path does.
    else return detail::componentwise(a, a, [](float p, float) noexcept {
        return from_bits(bits_of(p) & abs_mask);
    });
}

// Min and Max follow minps: `a < b ? a : b`, not the equivalent-looking
// `b < a ? b : a`. The two differ only when an operand is NaN, and writing it
// the other way is how the backend's own transposed version slipped through
// (docs/PLAN.md Phase 1 rule 3).
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type min(vector_type a, vector_type b) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(min(a.reg(), b.reg()));
    else return detail::componentwise(a, b, [](float p, float q) noexcept {
        return p < q ? p : q;
    });
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type max(vector_type a, vector_type b) noexcept {
    if constexpr (vector_type::lane_count == 4) return vector_type::from_reg(max(a.reg(), b.reg()));
    else return detail::componentwise(a, b, [](float p, float q) noexcept {
        return p > q ? p : q;
    });
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type clamp(vector_type v, vector_type lo, vector_type hi) noexcept {
    return min(max(v, lo), hi);
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type saturate(vector_type v) noexcept {
    return clamp(v, vector_type{}, vector_type{1.0f});
}

// (1 - t) * a + t * b, evaluated as a + t * (b - a): one fused multiply-add, and
// exact at t == 0 and t == 1.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type lerp(vector_type a, vector_type b, float t) noexcept {
    if constexpr (vector_type::lane_count == 4) {
        const vec_reg ra = a.reg();
        return vector_type::from_reg(mul_add(sub(b.reg(), ra), splat(t), ra));
    } else {
        return a + (b - a) * t;
    }
}

// ------------------------------------------------------------------ geometry
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float dot(vector_type a, vector_type b) noexcept {
    return get_x(detail::dot_lanes<vector_type::lane_count>(a.reg(), b.reg()));
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float length_sq(vector_type v) noexcept {
    return dot(v, v);
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float length(vector_type v) noexcept {
    const vec_reg r = v.reg();
    return get_x(sqrt(detail::dot_lanes<vector_type::lane_count>(r, r)));
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float distance_sq(vector_type a, vector_type b) noexcept {
    return length_sq(b - a);
}

template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float distance(vector_type a, vector_type b) noexcept {
    return length(b - a);
}

namespace detail {

// scalar_sqrt lives in mathematics/scalar.hpp -- the inverse trigonometric functions
// there need it too, and they must not depend on the vector layer.

// Branchless, for vector4. Every lane is computed and then selected.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type normalize_wide(vector_type v) noexcept {
    const vec_reg r = v.reg();
    const vec_reg length_sq = dot_lanes<vector_type::lane_count>(r, r);

    const vec_reg zero_value = zero();
    const vec_reg infinity = splat(consteval_ops::infinity);
    const mask_reg is_zero_length = cmp_eq(length_sq, zero_value);
    const mask_reg is_finite = cmp_ne(length_sq, infinity);

    // Divide by one where the length is zero. The quotient is discarded by the
    // select below either way, but 0/0 is undefined arithmetic and would abort
// constant evaluation before the select ever ran -- select is a blend, not a
    // branch, so both sides are always computed.
    const vec_reg divisor = select(is_zero_length, splat(1.0f), sqrt(length_sq));
    const vec_reg scaled = div(r, divisor);

    return vector_type::from_reg(select(is_zero_length, zero_value,
                             select(is_finite, scaled,
                                    splat(consteval_ops::quiet_nan))));
}

} // namespace detail

// Exact normalization without degenerate-input handling. The caller guarantees
// that the squared length is finite and greater than zero. Keeping this separate
// from normalize() makes the contract visible at hot call sites and lets packed
// vector2/vector3 values stay in scalar form instead of paying for a register
// round trip solely to compute their length.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type
normalize_unchecked(vector_type v) noexcept {
    float squared_length = v.x * v.x + v.y * v.y;
    if constexpr (vector_type::lane_count >= 3) {
        squared_length += v.z * v.z;
    }
    if constexpr (vector_type::lane_count == 4) {
        squared_length += v.w * v.w;
    }

    const float inverse_length = 1.0f / detail::scalar_sqrt(squared_length);
    v.x *= inverse_length;
    v.y *= inverse_length;
    if constexpr (vector_type::lane_count >= 3) v.z *= inverse_length;
    if constexpr (vector_type::lane_count == 4) v.w *= inverse_length;
    return v;
}

// Matches DirectXMath at the degenerate ends: a zero vector normalizes to zero
// rather than to NaN, and an infinite one to NaN. Getting that wrong is a silent
// source of NaN spreading through a scene graph, so it is worth paying for.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type normalize(vector_type v) noexcept {
    if constexpr (vector_type::lane_count < 4) {
        // Scalar for the narrow types, for the same reason the lane-wise
        // operators are: at two or three components the register round trip
        // costs more than it saves. The degenerate cases become branches rather
        // than selects, and they predict essentially perfectly -- a scene's
        // vectors are not usually zero-length or infinite.
        const float squared_length = length_sq(v);
        if (squared_length == 0.0f) return vector_type{};
        if (squared_length == consteval_ops::infinity) {
            return vector_type{consteval_ops::quiet_nan};
        }
        return v * (1.0f / detail::scalar_sqrt(squared_length));
    } else {
        return detail::normalize_wide(v);
    }
}

// The approximate form, with none of the guards above -- that is what makes it
// cheap. Feeding it a zero vector gives infinities.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vector_type normalize_est(vector_type v) noexcept {
    const vec_reg r = v.reg();
    return vector_type::from_reg(mul(r, rsqrt_est(detail::dot_lanes<vector_type::lane_count>(r, r))));
}

// Mirrors `incident` about the surface described by `normal`, which is assumed
// to be unit length. i - 2 * dot(i, n) * n.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type
reflect(vector_type incident, vector_type normal) noexcept {
    const vec_reg i = incident.reg();
    const vec_reg n = normal.reg();
    const vec_reg twice_dot = add(detail::dot_lanes<vector_type::lane_count>(i, n),
                                detail::dot_lanes<vector_type::lane_count>(i, n));
    return vector_type::from_reg(neg_mul_add(twice_dot, n, i));
}

// Snell's law. `refraction_index` is the ratio of the incident medium's index to
// the transmitted medium's. Total internal reflection returns the zero vector,
// which is what DirectXMath does and what callers branch on.
template <vector_like vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector_type
refract(vector_type incident, vector_type normal, float refraction_index) noexcept {
    const vec_reg i = incident.reg();
    const vec_reg n = normal.reg();
    const vec_reg eta = splat(refraction_index);

    const vec_reg dot_in = detail::dot_lanes<vector_type::lane_count>(i, n);
    // k = 1 - eta^2 * (1 - dot^2); negative means total internal reflection.
    const vec_reg one_minus_dot_sq = neg_mul_add(dot_in, dot_in, splat(1.0f));
    const vec_reg k = neg_mul_add(mul(eta, eta), one_minus_dot_sq, splat(1.0f));

    const vec_reg scale = mul_add(eta, dot_in, sqrt(k));
    const vec_reg refracted = neg_mul_add(scale, n, mul(eta, i));

    return vector_type::from_reg(select(cmp_le(k, zero()), zero(), refracted));
}

} // namespace math

#endif // MATHEMATICS_VECTOR_COMMON_HPP
