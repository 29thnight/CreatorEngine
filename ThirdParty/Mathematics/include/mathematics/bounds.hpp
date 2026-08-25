// mathematics/bounds.hpp — bounding volumes: Sphere and AABB.
//
// AABB stores a CENTRE AND HALF-WIDTHS, not a minimum and a maximum. That is
// DirectXMath's bounding_box layout, and it is the trap in this file: the two
// representations have the same shape -- two Vector3s -- so passing a min and a
// max to the constructor compiles and silently describes a different box.
// from_min_max exists precisely so that call site can say what it means, and the
// member names (center, extents) are chosen so a designated initializer reads
// unambiguously.
//
// Centre-and-extents is not an arbitrary inheritance. It makes the overlap test
// a subtraction and a comparison per axis with no branches, and it is what a
// separating-axis test wants; min/max would need a conversion at every query.
#ifndef MATHEMATICS_BOUNDS_HPP
#define MATHEMATICS_BOUNDS_HPP

#include <mathematics/vector.hpp>

#include <concepts>
#include <numeric>
#include <ranges>
#include <span>

namespace math {

namespace detail {

// Preserve the old single-add fast path for ordinary coordinates, but fall
// back to C++20's overflow-safe midpoint when that addition is non-finite.
// `sum - sum == 0` is true for every finite sum and false for infinity/NaN.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
overflow_safe_midpoint(float a, float b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return std::midpoint(a, b); }
    const float sum = a + b;
    if (sum - sum == 0.0f) return sum * 0.5f;
    return std::midpoint(a, b);
}

} // namespace detail

// How one volume sits relative to another. Note what Contains does NOT mean:
// `a.Contains(b)` says b is entirely inside a. A huge sphere that swallows a
// small box gives Contains(box, sphere) == Intersects, because the question is
// whether the BOX contains the sphere. DirectXMath uses the same vocabulary and
// the same asymmetry, and it is the first thing people get backwards.
enum class containment {
    disjoint,    // no overlap at all
    intersects,  // overlap, but the argument is not entirely inside
    contains,    // the argument is entirely inside the receiver
};

// -------------------------------------------------------------------- Sphere
struct sphere {
    vector3 center;
    float radius;

    constexpr sphere() noexcept : center{0.0f, 0.0f, 0.0f}, radius(0.0f) {}

    constexpr sphere(const vector3& center_in, float radius_in) noexcept
        : center(center_in), radius(radius_in) {}
};

static_assert(sizeof(sphere) == 16, "sphere must stay packed");
static_assert(std::is_standard_layout_v<sphere>);
static_assert(std::is_trivially_copyable_v<sphere>);

// ---------------------------------------------------------------------- AABB
struct aabb {
    vector3 center;
    vector3 extents;   // HALF-widths: the box spans center +/- extents

    // An empty box. A negative extent is the sentinel because zero extents are
    // a real point box; conflating the two made the old default participate in
    // Merge as the origin instead of acting as the identity.
    constexpr aabb() noexcept : center{0.0f, 0.0f, 0.0f},
                                extents{-1.0f, -1.0f, -1.0f} {}

    constexpr aabb(const vector3& center_in, const vector3& extents_in) noexcept
        : center(center_in), extents(extents_in) {}

    MATHEMATICS_NODISCARD constexpr bool is_empty() const noexcept {
        // Positive-form comparisons also reject NaN. An AABB with an unordered
        // extent cannot describe a volume and must not leak through as a hit.
        return !(extents.x >= 0.0f && extents.y >= 0.0f && extents.z >= 0.0f);
    }

    // Min/Max/Corner have no geometric meaning for an empty box. Check
    // IsEmpty() first; queries in this library do so before calling them.
    MATHEMATICS_NODISCARD constexpr vector3 min() const noexcept {
        return center - extents;
    }
    MATHEMATICS_NODISCARD constexpr vector3 max() const noexcept {
        return center + extents;
    }

    // The eight corners, indexed by bit: bit 0 is X, bit 1 is Y, bit 2 is Z,
    // set meaning the maximum side. So 0 is the minimum corner and 7 the
    // maximum, and iterating 0..7 visits every corner exactly once.
    MATHEMATICS_NODISCARD constexpr vector3 corner(int index) const noexcept {
        return vector3{
            center.x + ((index & 1) ? extents.x : -extents.x),
            center.y + ((index & 2) ? extents.y : -extents.y),
            center.z + ((index & 4) ? extents.z : -extents.z)};
    }

    MATHEMATICS_NODISCARD static constexpr aabb
    from_min_max(const vector3& minimum, const vector3& maximum) noexcept {
        // std::midpoint avoids overflowing the addition when both endpoints are
        // large and have the same sign. Measuring the extent from that centre
        // also handles the full [-FLT_MAX, +FLT_MAX] interval without first
        // forming the unrepresentable difference 2 * FLT_MAX.
        const vector3 midpoint{
            detail::overflow_safe_midpoint(minimum.x, maximum.x),
            detail::overflow_safe_midpoint(minimum.y, maximum.y),
            detail::overflow_safe_midpoint(minimum.z, maximum.z)};
        const vector3 lower_extent = midpoint - minimum;
        const vector3 upper_extent = maximum - midpoint;
        return aabb{midpoint, math::max(lower_extent, upper_extent)};
    }
};

static_assert(sizeof(aabb) == 24, "aabb is two packed Vector3s");
static_assert(std::is_standard_layout_v<aabb>);
static_assert(std::is_trivially_copyable_v<aabb>);
static_assert(aabb{}.is_empty());

// -------------------------------------------------------------------- growing
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
merge(const aabb& box, const vector3& point) noexcept {
    if (box.is_empty()) return aabb{point, vector3{0.0f, 0.0f, 0.0f}};
    return aabb::from_min_max(min(box.min(), point), max(box.max(), point));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
merge(const aabb& x, const aabb& y) noexcept {
    if (x.is_empty()) return y;
    if (y.is_empty()) return x;
    return aabb::from_min_max(min(x.min(), y.min()), max(x.max(), y.max()));
}

// Grown by a uniform margin on every side. A negative margin shrinks, and can
// drive the extents negative -- every query below then treats the box as empty
// rather than as one turned inside out.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
expand(const aabb& box, float margin) noexcept {
    if (box.is_empty()) return box;
    return aabb{box.center, box.extents + vector3{margin, margin, margin}};
}

// The tightest box around a run of points. An empty range gives an empty box at
// the origin, which Merge then treats as the identity.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
aabb_from_points(std::span<const vector3> points) noexcept {
    if (points.empty()) return aabb{};

    vector3 minimum = points.front();
    vector3 maximum = points.front();
    for (std::size_t i = 1; i < points.size(); ++i) {
        minimum = min(minimum, points[i]);
        maximum = max(maximum, points[i]);
    }
    return aabb::from_min_max(minimum, maximum);
}

// Keep the original pointer/count path as a direct indexed loop. It is a common
// engine hot path, and delegating it through a generic range abstraction would
// tie its code generation to standard-library implementation details.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
aabb_from_points(const vector3* points, int count) noexcept {
    if (points == nullptr || count <= 0) return aabb{};

    vector3 minimum = points[0];
    vector3 maximum = points[0];
    for (int i = 1; i < count; ++i) {
        minimum = min(minimum, points[i]);
        maximum = max(maximum, points[i]);
    }
    return aabb::from_min_max(minimum, maximum);
}

// Generic single-pass ranges do not require contiguous storage. The explicit
// span and pointer/count overloads above remain available so their code
// generation can be measured and kept stable independently.
template <std::ranges::input_range point_range>
    requires std::convertible_to<std::ranges::range_reference_t<point_range>, vector3>
MATHEMATICS_NODISCARD constexpr aabb
aabb_from_points(point_range&& points) {
    auto iterator = std::ranges::begin(points);
    const auto sentinel = std::ranges::end(points);
    if (iterator == sentinel) return aabb{};

    vector3 minimum = static_cast<vector3>(*iterator);
    vector3 maximum = minimum;
    for (++iterator; iterator != sentinel; ++iterator) {
        const vector3 point = static_cast<vector3>(*iterator);
        minimum = min(minimum, point);
        maximum = max(maximum, point);
    }
    return aabb::from_min_max(minimum, maximum);
}

// Grows a sphere just enough to swallow a point: the result touches both the
// old surface and the point, so its centre slides half the excess toward it.
// Growing only the radius would work too and would waste volume every time.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr sphere
merge(const sphere& input_sphere, const vector3& point) noexcept {
    const vector3 offset = point - input_sphere.center;
    const float distance = length(offset);
    if (distance <= input_sphere.radius) return input_sphere;

    const float new_radius =
        detail::overflow_safe_midpoint(input_sphere.radius, distance);
    const float t = (new_radius - input_sphere.radius) / distance;
    return sphere{input_sphere.center + offset * t, new_radius};
}

// The sphere around a box -- its centre, with the half-diagonal as radius.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr sphere
bounding_sphere(const aabb& box) noexcept {
    if (box.is_empty()) return sphere{};
    return sphere{box.center, length(box.extents)};
}

// The box around a sphere.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
bounding_box(const sphere& input_sphere) noexcept {
    return aabb{input_sphere.center,
                vector3{input_sphere.radius, input_sphere.radius,
                        input_sphere.radius}};
}

// --------------------------------------------------------------- closest point
// The point of the volume nearest the argument, which is the argument itself
// when it is already inside. This is the whole of the box-sphere test and half
// of several others, so it is named once rather than repeated.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
closest_point(const aabb& box, const vector3& point) noexcept {
    if (box.is_empty()) return point;
    return min(max(point, box.min()), box.max());
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
closest_point(const sphere& input_sphere, const vector3& point) noexcept {
    const vector3 offset = point - input_sphere.center;
    const float squared_length = length_sq(offset);
    if (squared_length <= input_sphere.radius * input_sphere.radius) return point;
    // A point exactly at the centre has no nearest surface point; the centre
    // is the one answer that is not a lie about direction.
    if (!detail::is_finite_non_zero(squared_length)) return input_sphere.center;
    return input_sphere.center +
           offset * (input_sphere.radius / detail::scalar_sqrt(squared_length));
}

// ------------------------------------------------------------------ comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const sphere& x, const sphere& y) noexcept {
    return x.center == y.center && x.radius == y.radius;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const aabb& x, const aabb& y) noexcept {
    return x.center == y.center && x.extents == y.extents;
}

// Positive tests, so a NaN fails rather than passing -- the matrix version of
// this shipped with the negated form and reported NaN as near-equal.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const sphere& x, const sphere& y, float epsilon = 1e-5f) noexcept {
    const float dr = x.radius - y.radius;
    return near_equal(x.center, y.center, epsilon) &&
           dr <= epsilon && dr >= -epsilon;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const aabb& x, const aabb& y, float epsilon = 1e-5f) noexcept {
    return near_equal(x.center, y.center, epsilon) &&
           near_equal(x.extents, y.extents, epsilon);
}

} // namespace math

#endif // MATHEMATICS_BOUNDS_HPP
