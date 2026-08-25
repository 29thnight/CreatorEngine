// mathematics/vector2.hpp — packed two-component vector.
#ifndef MATHEMATICS_VECTOR2_HPP
#define MATHEMATICS_VECTOR2_HPP

#include <mathematics/vector_common.hpp>

namespace math {

// Eight bytes, standard layout. Arithmetic lives in vector_common.hpp; only
// storage, conversions, constants and the 2D cross are here.
struct vector2 {
    float x, y;

    static constexpr int lane_count = 2;

    constexpr vector2() noexcept : x(0.0f), y(0.0f) {}

    constexpr vector2(float x_in, float y_in) noexcept : x(x_in), y(y_in) {}

    explicit constexpr vector2(float s) noexcept : x(s), y(s) {}

    // ------------------------------------------------------------ conversions
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg reg() const noexcept {
        return set(x, y, 0.0f, 0.0f);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static constexpr vector2
    from_reg(vec_reg r) noexcept {
        return vector2{get_x(r), get_y(r)};
    }

    // ------------------------------------------------------------- lane access
    MATHEMATICS_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    // --------------------------------------------------------------- constants
    MATHEMATICS_NODISCARD static constexpr vector2 zero() noexcept {
        return vector2{0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector2 one() noexcept {
        return vector2{1.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector2 unit_x() noexcept {
        return vector2{1.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr vector2 unit_y() noexcept {
        return vector2{0.0f, 1.0f};
    }
};

static_assert(sizeof(vector2) == 8, "vector2 must stay packed");
static_assert(std::is_standard_layout_v<vector2>);
static_assert(std::is_trivially_copyable_v<vector2>);

// The 2D cross product is a scalar -- the z component the 3D cross would
// produce. Positive when b lies counter-clockwise from a, which makes it the
// usual winding and side-of-line test.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
cross(vector2 a, vector2 b) noexcept {
    return a.x * b.y - a.y * b.x;
}

// Rotated a quarter turn counter-clockwise. Common enough in 2D work to be
// worth a name rather than an open-coded swap and negate.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector2 perpendicular(vector2 v) noexcept {
    return vector2{-v.y, v.x};
}

} // namespace math

#endif // MATHEMATICS_VECTOR2_HPP
