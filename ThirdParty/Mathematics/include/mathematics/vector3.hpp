// mathematics/vector3.hpp — packed three-component vector.
#ifndef MATHEMATICS_VECTOR3_HPP
#define MATHEMATICS_VECTOR3_HPP

#include <mathematics/vector_common.hpp>

namespace math {

// Twelve bytes, standard layout. The packing is the point: a vector3 array is a
// position stream a GPU can read directly, with no gaps to strip out first.
//
// Arithmetic lives in vector_common.hpp. Only storage, conversions, constants
// and the cross product are here.
struct vector3 {
    float x, y, z;

    static constexpr int lane_count = 3;

    constexpr vector3() noexcept : x(0.0f), y(0.0f), z(0.0f) {}

    constexpr vector3(float x_in, float y_in, float z_in) noexcept
        : x(x_in), y(y_in), z(z_in) {}

    explicit constexpr vector3(float s) noexcept : x(s), y(s), z(s) {}

    // ------------------------------------------------------------ conversions
    // The register's w lane is zero. Every operation that consumes a vector3
    // uses dot3, and from_reg drops w, so it never reaches a result -- but it
    // being zero rather than indeterminate keeps length and normalize honest if
    // one ever slips through a dot4.
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg reg() const noexcept {
        // Deliberately not a 16-byte load of &x: the object is twelve bytes, so
        // that would read past the end and can fault at a page boundary.
        MATHEMATICS_IF_CONSTEVAL { return set(x, y, z, 0.0f); }
#if MATHEMATICS_COMPILER_CLANG && MATHEMATICS_SIMD_SSE
        // Clang otherwise reloads y/z across the 8+4-byte store boundary in a
        // dependent chain, defeating store-to-load forwarding. MSVC generates
        // better aggregate and array code from set, so keep its established path.
        return load3(static_cast<const void*>(this));
#else
        return set(x, y, z, 0.0f);
#endif
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static constexpr vector3
    from_reg(vec_reg r) noexcept {
        MATHEMATICS_IF_CONSTEVAL { return vector3{get_x(r), get_y(r), get_z(r)}; }
#if MATHEMATICS_SIMD_SSE && MATHEMATICS_COMPILER_MSVC
        // MSVC only, and the mirror image of the split in reg() above. It emits
        // a shuffle per lane for the three-scalar form, which is two more than
        // the store needs; store3 says the same thing in one 8-byte store and
        // one fused extract.
        //
        // Clang must not take this path. It already folds the lane-wise form
        // into exactly these two instructions, and going through memory instead
        // costs it the register-resident chain: a serial cross, where the
        // result feeds straight back into reg(), then pays a store-to-load
        // round trip it did not pay before. Measured at 6.45 -> 10.68 ns.
        vector3 result;
        store3(&result, r);
        return result;
#else
        return vector3{get_x(r), get_y(r), get_z(r)};
#endif
    }

    // ------------------------------------------------------------- lane access
    MATHEMATICS_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    // --------------------------------------------------------------- constants
    MATHEMATICS_NODISCARD static constexpr vector3 zero() noexcept {
        return vector3{0.0f, 0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector3 one() noexcept {
        return vector3{1.0f, 1.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector3 unit_x() noexcept {
        return vector3{1.0f, 0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector3 unit_y() noexcept {
        return vector3{0.0f, 1.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector3 unit_z() noexcept {
        return vector3{0.0f, 0.0f, 1.0f};
    }
};

static_assert(sizeof(vector3) == 12, "vector3 must stay packed");
static_assert(std::is_standard_layout_v<vector3>);
static_assert(std::is_trivially_copyable_v<vector3>);

// ---------------------------------------------------------------- cross product
// Right-handed: cross(unit_x, unit_y) == unit_z.
//
// Computed with two shuffles rather than the textbook three, by evaluating the
// permuted product once and rotating the result -- the same trick DirectXMath
// uses in XMVector3Cross.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
cross(vector3 a, vector3 b) noexcept {
    const vec_reg ra = a.reg();
    const vec_reg rb = b.reg();

    // (a.yzx * b.zxy) - (a.zxy * b.yzx). Reusing the first two
    // permutations to form the second pair matches XMVector3Cross's dependency
    // shape and keeps Clang from selecting a longer serial schedule.
    vec_reg a_permuted = shuffle<1, 2, 0, 3>(ra);
    vec_reg b_permuted = shuffle<2, 0, 1, 3>(rb);
    const vec_reg result = mul(a_permuted, b_permuted);
    a_permuted = shuffle<1, 2, 0, 3>(a_permuted);
    b_permuted = shuffle<2, 0, 1, 3>(b_permuted);

    // Spell the subtraction as a negative multiply-add so AVX2 emits the same
    // vmulps + vfnmaddps pair as DirectXMath instead of two multiplies and a
    // separate subtract when the compiler declines to contract intrinsics.
    return vector3::from_reg(neg_mul_add(a_permuted, b_permuted, result));
}

} // namespace math

#endif // MATHEMATICS_VECTOR3_HPP
