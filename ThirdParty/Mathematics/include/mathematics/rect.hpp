// mathematics/rect.hpp — a float 2D rectangle stored as x/y/width/height.
//
// Rectangles use half-open point containment: the minimum edges belong to the
// rectangle and the maximum edges do not. Adjacent UI rectangles therefore do
// not both claim a pixel on their shared edge. For the same reason two
// rectangles that only touch at an edge do not overlap: their intersection has
// zero area.
#ifndef MATHEMATICS_RECT_HPP
#define MATHEMATICS_RECT_HPP

#include <mathematics/vector.hpp>

namespace math {

struct rect {
    float x, y, width, height;

    constexpr rect() noexcept
        : x(0.0f), y(0.0f), width(0.0f), height(0.0f) {}

    constexpr rect(float x_in, float y_in, float width_in,
                   float height_in) noexcept
        : x(x_in), y(y_in), width(width_in), height(height_in) {}

    constexpr rect(const vector2& position, const vector2& size) noexcept
        : x(position.x), y(position.y), width(size.x), height(size.y) {}

    MATHEMATICS_NODISCARD constexpr vector2 position() const noexcept {
        return vector2{x, y};
    }
    MATHEMATICS_NODISCARD constexpr vector2 size() const noexcept {
        return vector2{width, height};
    }
    MATHEMATICS_NODISCARD constexpr vector2 min() const noexcept {
        return vector2{x, y};
    }
    MATHEMATICS_NODISCARD constexpr vector2 max() const noexcept {
        return vector2{x + width, y + height};
    }
    MATHEMATICS_NODISCARD constexpr vector2 center() const noexcept {
        return vector2{x + width * 0.5f, y + height * 0.5f};
    }
    MATHEMATICS_NODISCARD constexpr float area() const noexcept {
        return is_empty() ? 0.0f : width * height;
    }
    MATHEMATICS_NODISCARD constexpr bool is_empty() const noexcept {
        // Positive form also rejects NaN dimensions.
        return !(width > 0.0f && height > 0.0f);
    }

    MATHEMATICS_NODISCARD static constexpr rect
    from_min_max(const vector2& minimum, const vector2& maximum) noexcept {
        return rect{minimum, maximum - minimum};
    }
};

static_assert(sizeof(rect) == 16, "rect must stay four packed floats");
static_assert(std::is_standard_layout_v<rect>);
static_assert(std::is_trivially_copyable_v<rect>);

// Turns a negative width or height around while preserving the represented
// set. This is explicit rather than automatic: silently normalizing every
// query would hide a common layout bug and add branches to the hot path.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr rect
normalized(const rect& value) noexcept {
    const float minimum_x = value.width < 0.0f ? value.x + value.width : value.x;
    const float minimum_y = value.height < 0.0f ? value.y + value.height : value.y;
    const float positive_width = value.width < 0.0f ? -value.width : value.width;
    const float positive_height = value.height < 0.0f ? -value.height : value.height;
    return rect{minimum_x, minimum_y, positive_width, positive_height};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
contains(const rect& outer, const vector2& point) noexcept {
    if (outer.is_empty()) return false;
    return point.x >= outer.x && point.x < outer.x + outer.width &&
           point.y >= outer.y && point.y < outer.y + outer.height;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
contains(const rect& outer, const rect& inner) noexcept {
    if (outer.is_empty() || inner.is_empty()) return false;
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const rect& x, const rect& y) noexcept {
    if (x.is_empty() || y.is_empty()) return false;
    return y.x < x.x + x.width && x.x < y.x + y.width &&
           y.y < x.y + x.height && x.y < y.y + y.height;
}

// The overlapped area. Disjoint or merely touching rectangles produce the
// canonical empty rectangle at the origin.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr rect
intersection(const rect& x, const rect& y) noexcept {
    if (!intersects(x, y)) return rect{};
    const vector2 minimum = math::max(x.min(), y.min());
    const vector2 maximum = math::min(x.max(), y.max());
    return rect::from_min_max(minimum, maximum);
}

// The smallest rectangle covering both inputs. Empty rectangles are identities
// rather than points at their stored positions.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr rect
merge(const rect& x, const rect& y) noexcept {
    if (x.is_empty()) return y;
    if (y.is_empty()) return x;
    return rect::from_min_max(math::min(x.min(), y.min()),
                              math::max(x.max(), y.max()));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr rect
offset(const rect& value, const vector2& amount) noexcept {
    return rect{value.position() + amount, value.size()};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr rect
inflate(const rect& value, const vector2& amount) noexcept {
    if (value.is_empty()) return value;
    return rect{value.x - amount.x, value.y - amount.y,
                value.width + amount.x + amount.x,
                value.height + amount.y + amount.y};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector2
closest_point(const rect& value, const vector2& point) noexcept {
    if (value.is_empty()) return point;
    return math::min(math::max(point, value.min()), value.max());
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const rect& x, const rect& y) noexcept {
    return x.x == y.x && x.y == y.y &&
           x.width == y.width && x.height == y.height;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const rect& x, const rect& y, float epsilon = 1e-5f) noexcept {
    const float dx = x.x - y.x, dy = x.y - y.y;
    const float dw = x.width - y.width, dh = x.height - y.height;
    return dx <= epsilon && dx >= -epsilon &&
           dy <= epsilon && dy >= -epsilon &&
           dw <= epsilon && dw >= -epsilon &&
           dh <= epsilon && dh >= -epsilon;
}

} // namespace math

#endif // MATHEMATICS_RECT_HPP
