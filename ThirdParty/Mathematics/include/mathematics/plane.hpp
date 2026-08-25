// mathematics/plane.hpp — the plane ax + by + cz + d = 0.
//
// Storage and sign follow DirectXMath, observed rather than assumed:
//
//   * A plane is four floats (a, b, c, d). The first three are the normal.
//   * d is MINUS the dot of the normal with any point on the plane, so that
//     signed_distance(p) is just dot(n, p) + d.
//   * A positive signed distance means the point is on the side the normal
//     points to. That side is called the FRONT.
//   * from_points uses the right-hand rule: the normal is
//     cross(b - a, c - a), so a triangle wound counter-clockwise when seen
//     from the front faces you.
//
// The normal is NOT normalized on construction unless the factory says so.
// from_point_normal and from_points both produce a unit normal; the four-float
// constructor does not, because a caller assembling a plane from a projection
// matrix row has a valid unnormalized plane and normalizing it would be wrong.
// Only a plane with a unit normal gives a true distance; normalize makes one.
#ifndef MATHEMATICS_PLANE_HPP
#define MATHEMATICS_PLANE_HPP

#include <mathematics/vector.hpp>

namespace math {

// Which side of a plane something is on. INTERSECTING in DirectXMath's
// vocabulary; Straddling here, because a volume that crosses the plane is not
// "intersecting the plane" in the same sense that two volumes intersect, and
// the tests read better for the distinction.
enum class plane_side {
    front,       // entirely on the side the normal points to
    back,        // entirely on the other side
    straddling,  // crosses the plane
};

struct plane {
    float a, b, c, d;

    // The XY plane through the origin, facing +Z. A default plane has to be a
    // real plane -- all zeros is not one, and would make every query nonsense.
    constexpr plane() noexcept : a(0.0f), b(0.0f), c(1.0f), d(0.0f) {}

    constexpr plane(float a_in, float b_in, float c_in, float d_in) noexcept
        : a(a_in), b(b_in), c(c_in), d(d_in) {}

    MATHEMATICS_NODISCARD constexpr vector3 normal() const noexcept {
        return vector3{a, b, c};
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg reg() const noexcept {
        MATHEMATICS_IF_CONSTEVAL { return set(a, b, c, d); }
        return load(&a);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static constexpr plane
    from_reg(vec_reg r) noexcept {
        MATHEMATICS_IF_CONSTEVAL {
            return plane{lane(r, 0), lane(r, 1), lane(r, 2), lane(r, 3)};
        }
        plane out;
        store(&out.a, r);
        return out;
    }
};

static_assert(sizeof(plane) == 16, "plane must stay packed");
static_assert(std::is_standard_layout_v<plane>);
static_assert(std::is_trivially_copyable_v<plane>);

// ------------------------------------------------------------------ factories
// A degenerate normal has no plane to describe, so this returns the default XY
// plane rather than one full of NaN -- the same choice normalize and inverse
// make elsewhere in the library.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane
plane_from_point_normal(const vector3& point, const vector3& normal) noexcept {
    const float squared_length = length_sq(normal);
    if (!detail::is_finite_non_zero(squared_length)) return plane{};

    const float inv = 1.0f / detail::scalar_sqrt(squared_length);
    const vector3 n{normal.x * inv, normal.y * inv, normal.z * inv};
    return plane{n.x, n.y, n.z, -dot(n, point)};
}

// Right-handed: cross(b - a, c - a). Three collinear points span no plane and
// give the default, for the same reason as above.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane
plane_from_points(const vector3& p0, const vector3& p1,
                const vector3& p2) noexcept {
    return plane_from_point_normal(p0, cross(p1 - p0, p2 - p0));
}

// Rescales so the normal is unit and d is a true distance. A plane built from
// a matrix row generally is not normalized, and every distance query is wrong
// by the normal's length until it is.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane
normalize(const plane& input_plane) noexcept {
    const float squared_length = input_plane.a * input_plane.a +
                                 input_plane.b * input_plane.b +
                                 input_plane.c * input_plane.c;
    if (!detail::is_finite_non_zero(squared_length)) return plane{};

    const float inv = 1.0f / detail::scalar_sqrt(squared_length);
    return plane{input_plane.a * inv, input_plane.b * inv,
                 input_plane.c * inv, input_plane.d * inv};
}

// ------------------------------------------------------------------- queries
// Positive in front, negative behind, zero on the plane. A true distance only
// when the normal is unit -- see the header note.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
signed_distance(const plane& input_plane, const vector3& point) noexcept {
    return input_plane.a * point.x + input_plane.b * point.y +
           input_plane.c * point.z + input_plane.d;
}

// The plane equation applied to a DIRECTION, which ignores d. Useful for
// asking whether a direction points across a plane without caring where the
// plane sits -- a backface test, for instance.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
dot_normal(const plane& input_plane, const vector3& direction) noexcept {
    return input_plane.a * direction.x + input_plane.b * direction.y +
           input_plane.c * direction.z;
}

// The epsilon is a band around the plane counted as "on it", in the same units
// as the distance -- so it means what a caller expects only for a unit normal.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane_side
classify_point(const plane& input_plane, const vector3& point,
              float epsilon = 1e-5f) noexcept {
    const float distance = signed_distance(input_plane, point);
    if (distance > epsilon) return plane_side::front;
    if (distance < -epsilon) return plane_side::back;
    return plane_side::straddling;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
closest_point_on_plane(const plane& input_plane, const vector3& point) noexcept {
    const float squared_length = input_plane.a * input_plane.a +
                                 input_plane.b * input_plane.b +
                                 input_plane.c * input_plane.c;
    if (!detail::is_finite_non_zero(squared_length)) return point;
    return point - input_plane.normal() *
                       (signed_distance(input_plane, point) / squared_length);
}

// Mirrors a point through the plane. Reflecting a DIRECTION is Reflect() in
// vector_common.hpp -- a direction has no position, so d must not enter it,
// and using the wrong one of the two is a bug that looks almost right.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
reflect_point(const plane& input_plane, const vector3& point) noexcept {
    const float squared_length = input_plane.a * input_plane.a +
                                 input_plane.b * input_plane.b +
                                 input_plane.c * input_plane.c;
    if (!detail::is_finite_non_zero(squared_length)) return point;
    return point - input_plane.normal() *
                       (2.0f * signed_distance(input_plane, point) /
                        squared_length);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane
flip(const plane& input_plane) noexcept {
    return plane{-input_plane.a, -input_plane.b,
                 -input_plane.c, -input_plane.d};
}

// ---------------------------------------------------------------- comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const plane& x, const plane& y) noexcept {
    return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d;
}

// Positive test, so a NaN fails rather than passing -- the matrix version of
// this shipped with the negated form and reported NaN as near-equal.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const plane& x, const plane& y, float epsilon = 1e-5f) noexcept {
    const float da = x.a - y.a, db = x.b - y.b;
    const float dc = x.c - y.c, dd = x.d - y.d;
    return da <= epsilon && da >= -epsilon && db <= epsilon && db >= -epsilon &&
           dc <= epsilon && dc >= -epsilon && dd <= epsilon && dd >= -epsilon;
}

} // namespace math

#endif // MATHEMATICS_PLANE_HPP
