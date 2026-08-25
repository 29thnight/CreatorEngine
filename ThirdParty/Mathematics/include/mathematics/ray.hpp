// mathematics/ray.hpp — a half-line: an origin and a direction.
//
// A ray starts at its origin and goes one way only. Every intersection query
// in intersect.hpp returns a distance measured ALONG THE DIRECTION, so:
//
//   * A hit behind the origin is not a hit. A ray fired away from a sphere
//     reports no intersection, not a negative distance.
//   * A ray whose origin is already inside a volume hits at distance zero,
//     because it is touching that volume at its origin.
//
// That last rule is the one place in this library that deliberately does NOT
// match DirectXMath, for the unusual reason that DirectXMath does not match
// itself. Fired along +Z from inside a unit sphere at (0,0,0.5), its
// bounding_sphere reports 0.5 -- the distance to where the ray LEAVES. The same
// query against a bounding_box spanning [-1,1] reports -1.5 -- the distance
// BACK to where the ray would have entered. One is a positive exit, the other
// a negative entry, and no caller can write one branch that handles both. So
// the two conventions cannot both be matched, and zero is the answer that is
// true for every primitive, never points behind the origin, and needs no
// special case at the call site. Ask Intersects(volume, ray.origin) to tell
// whether a reported zero means "started inside".
//   * The distance is in units of the direction's length. Keep the direction
//     normalized and it is a real distance; leave it unnormalized and it is a
//     parameter t such that Origin + Direction * t is the hit point. Both are
//     useful, so neither is forced, but mixing them up silently scales every
//     distance the caller reads.
#ifndef MATHEMATICS_RAY_HPP
#define MATHEMATICS_RAY_HPP

#include <mathematics/vector.hpp>

namespace math {

struct ray {
    vector3 origin;
    vector3 direction;

    // Along +Z from the origin -- a real ray, so a default-constructed one can
    // be queried without producing nonsense the way a zero direction would.
    constexpr ray() noexcept : origin{0.0f, 0.0f, 0.0f},
                               direction{0.0f, 0.0f, 1.0f} {}

    constexpr ray(const vector3& origin_in, const vector3& direction_in) noexcept
        : origin(origin_in), direction(direction_in) {}

    MATHEMATICS_NODISCARD constexpr vector3 point_at(float t) const noexcept {
        return origin + direction * t;
    }
};

static_assert(sizeof(ray) == 24, "ray is two packed Vector3s");
static_assert(std::is_standard_layout_v<ray>);
static_assert(std::is_trivially_copyable_v<ray>);

// A zero direction has no ray to describe, so this leaves the default +Z
    // rather than dividing by zero -- consistent with normalize elsewhere.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr ray
normalize_direction(const ray& input_ray) noexcept {
    const float squared_length = length_sq(input_ray.direction);
    if (!detail::is_finite_non_zero(squared_length)) {
        return ray{input_ray.origin, vector3{0.0f, 0.0f, 1.0f}};
    }
    return ray{input_ray.origin,
               input_ray.direction *
                   (1.0f / detail::scalar_sqrt(squared_length))};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const ray& x, const ray& y) noexcept {
    return x.origin == y.origin && x.direction == y.direction;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const ray& x, const ray& y, float epsilon = 1e-5f) noexcept {
    return near_equal(x.origin, y.origin, epsilon) &&
           near_equal(x.direction, y.direction, epsilon);
}

} // namespace math

#endif // MATHEMATICS_RAY_HPP
