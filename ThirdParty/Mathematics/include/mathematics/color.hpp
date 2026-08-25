// mathematics/color.hpp — linear RGBA colour storage and colour operations.
//
// A colour is deliberately its own type rather than an alias for vector4. The
// layout is identical, but the names are not: r/g/b/a make call sites readable,
// and operations such as premultiply and adjust_saturation have colour meaning.
// Values are linear floats unless the caller explicitly converts at an I/O
// boundary; this type does not silently apply an sRGB transfer function.
#ifndef MATHEMATICS_COLOR_HPP
#define MATHEMATICS_COLOR_HPP

#include <mathematics/vector.hpp>

#include <cstdint>

namespace math {

struct color {
    float r, g, b, a;

    // Opaque black matches the useful rendering default (and DirectXTK's
    // SimpleMath::Color), rather than vector4's all-zero default.
    constexpr color() noexcept : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}

    constexpr color(float red, float green, float blue,
                    float alpha = 1.0f) noexcept
        : r(red), g(green), b(blue), a(alpha) {}

    explicit constexpr color(const vector3& rgb, float alpha = 1.0f) noexcept
        : r(rgb.x), g(rgb.y), b(rgb.z), a(alpha) {}

    explicit constexpr color(const vector4& rgba) noexcept
        : r(rgba.x), g(rgba.y), b(rgba.z), a(rgba.w) {}

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg reg() const noexcept {
        MATHEMATICS_IF_CONSTEVAL { return set(r, g, b, a); }
        return load(&r);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static constexpr color
    from_reg(vec_reg value) noexcept {
        MATHEMATICS_IF_CONSTEVAL {
            return color{lane(value, 0), lane(value, 1),
                         lane(value, 2), lane(value, 3)};
        }
        color result;
        store(&result.r, value);
        return result;
    }

    MATHEMATICS_NODISCARD constexpr float operator[](int index) const noexcept {
        return (&r)[index];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int index) noexcept {
        return (&r)[index];
    }

    MATHEMATICS_NODISCARD constexpr vector3 rgb() const noexcept {
        return vector3{r, g, b};
    }
    MATHEMATICS_NODISCARD constexpr vector4 rgba() const noexcept {
        return vector4{r, g, b, a};
    }

    MATHEMATICS_NODISCARD static constexpr color transparent() noexcept {
        return color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    MATHEMATICS_NODISCARD static constexpr color black() noexcept {
        return color{0.0f, 0.0f, 0.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr color white() noexcept {
        return color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr color red() noexcept {
        return color{1.0f, 0.0f, 0.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr color green() noexcept {
        return color{0.0f, 1.0f, 0.0f, 1.0f};
    }
    MATHEMATICS_NODISCARD static constexpr color blue() noexcept {
        return color{0.0f, 0.0f, 1.0f, 1.0f};
    }
};

static_assert(sizeof(color) == 16, "color must stay packed RGBA");
static_assert(std::is_standard_layout_v<color>);
static_assert(std::is_trivially_copyable_v<color>);

// ---------------------------------------------------------------- arithmetic
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator+(const color& x, const color& y) noexcept {
    return color::from_reg(add(x.reg(), y.reg()));
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator-(const color& x, const color& y) noexcept {
    return color::from_reg(sub(x.reg(), y.reg()));
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator*(const color& x, const color& y) noexcept {
    return color::from_reg(mul(x.reg(), y.reg()));
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator/(const color& x, const color& y) noexcept {
    return color::from_reg(div(x.reg(), y.reg()));
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator*(const color& value, float scalar) noexcept {
    return color::from_reg(mul(value.reg(), splat(scalar)));
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator*(float scalar, const color& value) noexcept {
    return value * scalar;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator/(const color& value, float scalar) noexcept {
    return color::from_reg(div(value.reg(), splat(scalar)));
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
operator-(const color& value) noexcept {
    return color::from_reg(negate(value.reg()));
}

MATHEMATICS_INLINE constexpr color& operator+=(color& x, const color& y) noexcept {
    return x = x + y;
}
MATHEMATICS_INLINE constexpr color& operator-=(color& x, const color& y) noexcept {
    return x = x - y;
}
MATHEMATICS_INLINE constexpr color& operator*=(color& x, const color& y) noexcept {
    return x = x * y;
}
MATHEMATICS_INLINE constexpr color& operator*=(color& value, float scalar) noexcept {
    return value = value * scalar;
}
MATHEMATICS_INLINE constexpr color& operator/=(color& x, const color& y) noexcept {
    return x = x / y;
}
MATHEMATICS_INLINE constexpr color& operator/=(color& value, float scalar) noexcept {
    return value = value / scalar;
}

// ----------------------------------------------------------- colour operations
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
saturate(const color& value) noexcept {
    return color::from_reg(min(max(value.reg(), splat(0.0f)), splat(1.0f)));
}

// RGB is multiplied by alpha; alpha itself is retained.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
premultiply(const color& value) noexcept {
    return color{value.r * value.a, value.g * value.a,
                 value.b * value.a, value.a};
}

// The linear-light luminance weights used by DirectXMath's colour helpers.
// Saturation 0 produces grey, 1 is the identity, and values above 1 exaggerate
// colour differences without clamping them.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
adjust_saturation(const color& value, float saturation) noexcept {
    const float luminance = value.r * 0.2125f + value.g * 0.7154f +
                            value.b * 0.0721f;
    return color{luminance + (value.r - luminance) * saturation,
                 luminance + (value.g - luminance) * saturation,
                 luminance + (value.b - luminance) * saturation,
                 value.a};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
adjust_contrast(const color& value, float contrast) noexcept {
    return color{0.5f + (value.r - 0.5f) * contrast,
                 0.5f + (value.g - 0.5f) * contrast,
                 0.5f + (value.b - 0.5f) * contrast,
                 value.a};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
negative(const color& value) noexcept {
    return color{1.0f - value.r, 1.0f - value.g,
                 1.0f - value.b, value.a};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
modulate(const color& x, const color& y) noexcept {
    return x * y;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
lerp(const color& x, const color& y, float t) noexcept {
    return color::from_reg(mul_add(splat(t), sub(y.reg(), x.reg()), x.reg()));
}

namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::uint32_t
color_byte(float value) noexcept {
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
}

} // namespace detail

// Numeric layout is 0xAABBGGRR: in little-endian memory that is RGBA byte
// order, which is the useful form for an R8G8B8A8 upload.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::uint32_t
pack_rgba8(const color& value) noexcept {
    return detail::color_byte(value.r) |
           (detail::color_byte(value.g) << 8) |
           (detail::color_byte(value.b) << 16) |
           (detail::color_byte(value.a) << 24);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
unpack_rgba8(std::uint32_t packed) noexcept {
    constexpr float scale = 1.0f / 255.0f;
    return color{static_cast<float>(packed & 0xffu) * scale,
                 static_cast<float>((packed >> 8) & 0xffu) * scale,
                 static_cast<float>((packed >> 16) & 0xffu) * scale,
                 static_cast<float>((packed >> 24) & 0xffu) * scale};
}

// Numeric layout is 0xAARRGGBB: little-endian BGRA byte order.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::uint32_t
pack_bgra8(const color& value) noexcept {
    return detail::color_byte(value.b) |
           (detail::color_byte(value.g) << 8) |
           (detail::color_byte(value.r) << 16) |
           (detail::color_byte(value.a) << 24);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr color
unpack_bgra8(std::uint32_t packed) noexcept {
    constexpr float scale = 1.0f / 255.0f;
    return color{static_cast<float>((packed >> 16) & 0xffu) * scale,
                 static_cast<float>((packed >> 8) & 0xffu) * scale,
                 static_cast<float>(packed & 0xffu) * scale,
                 static_cast<float>((packed >> 24) & 0xffu) * scale};
}

// ---------------------------------------------------------------- comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const color& x, const color& y) noexcept {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const color& x, const color& y, float epsilon = 1e-5f) noexcept {
    const float dr = x.r - y.r, dg = x.g - y.g;
    const float db = x.b - y.b, da = x.a - y.a;
    return dr <= epsilon && dr >= -epsilon &&
           dg <= epsilon && dg >= -epsilon &&
           db <= epsilon && db >= -epsilon &&
           da <= epsilon && da >= -epsilon;
}

} // namespace math

#endif // MATHEMATICS_COLOR_HPP
