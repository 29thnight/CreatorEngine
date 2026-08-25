// mathematics/vector4.hpp — packed four-component vector.
#ifndef MATHEMATICS_VECTOR4_HPP
#define MATHEMATICS_VECTOR4_HPP

#include <mathematics/vector_common.hpp>

namespace math {

// Sixteen bytes, standard layout, four-byte aligned -- it drops into vertex
// buffers, constant buffers and struct members with no padding and no alignment
// demands on whatever contains it.
//
// Arithmetic lives in vector_common.hpp, as free templates shared with vector2
// and vector3. Only the storage, the conversions, and the constants are here.
struct vector4 {
    float x, y, z, w;

    static constexpr int lane_count = 4;

    // Default-constructs to zero rather than leaving the lanes indeterminate.
    // DirectXMath and GLM both leave them uninitialized by default; the cost of
    // zeroing is near nothing next to a whole class of silent uninitialized-read
    // bugs, and a compiler drops it wherever the value is overwritten anyway.
    constexpr vector4() noexcept : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}

    constexpr vector4(float x_in, float y_in, float z_in, float w_in) noexcept
        : x(x_in), y(y_in), z(z_in), w(w_in) {}

    explicit constexpr vector4(float s) noexcept : x(s), y(s), z(s), w(s) {}

    // ------------------------------------------------------------ conversions
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg reg() const noexcept {
        MATHEMATICS_IF_CONSTEVAL { return set(x, y, z, w); }
        // Members are contiguous and the object is a full sixteen bytes, so one
        // unaligned load reaches all four.
        return load(&x);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static constexpr vector4
    from_reg(vec_reg r) noexcept {
        MATHEMATICS_IF_CONSTEVAL {
            return vector4{lane(r, 0), lane(r, 1), lane(r, 2), lane(r, 3)};
        }
        vector4 out;
        store(&out.x, r);
        return out;
    }

    // ------------------------------------------------------------- lane access
    MATHEMATICS_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    // --------------------------------------------------------------- constants
    MATHEMATICS_NODISCARD static constexpr vector4 zero() noexcept {
        return vector4{0.0f, 0.0f, 0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector4 one() noexcept {
        return vector4{1.0f, 1.0f, 1.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector4 unit_x() noexcept {
        return vector4{1.0f, 0.0f, 0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector4 unit_y() noexcept {
        return vector4{0.0f, 1.0f, 0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector4 unit_z() noexcept {
        return vector4{0.0f, 0.0f, 1.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector4 unit_w() noexcept {
        return vector4{0.0f, 0.0f, 0.0f, 1.0f};
    }
};

static_assert(sizeof(vector4) == 16, "vector4 must stay packed");
static_assert(std::is_standard_layout_v<vector4>);
static_assert(std::is_trivially_copyable_v<vector4>);

} // namespace math

#endif // MATHEMATICS_VECTOR4_HPP
