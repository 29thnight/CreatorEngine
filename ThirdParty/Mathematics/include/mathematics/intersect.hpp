// mathematics/intersect.hpp — queries between two geometric primitives.
//
// Separated from the types themselves because every one of these needs two of
// them, and putting a sphere-versus-box test in either header would make that
// header depend on the other for no reason.
//
// Three shapes of answer, and which one a function returns is part of its name:
//
//   Intersects(a, b) -> bool          do they touch at all
//   Contains(a, b)   -> containment   is b inside a, touching a, or apart
//   Classify(a, p)   -> plane_side     which side of plane p does a sit on
//   Raycast(...)     -> bool + float  does the ray hit, and how far along
//
// Touching counts as intersecting throughout: a sphere resting exactly on a
// plane, or two boxes sharing a face, are reported as intersecting rather than
// disjoint. Any other choice makes the predicate discontinuous in a way that
// shows up as objects flickering apart at exact contact.
#ifndef MATHEMATICS_INTERSECT_HPP
#define MATHEMATICS_INTERSECT_HPP

#include <mathematics/bounds.hpp>
#include <mathematics/frustum.hpp>
#include <mathematics/plane.hpp>
#include <mathematics/ray.hpp>

#include <optional>

namespace math {

// ---------------------------------------------------------------- containment
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const sphere& input_sphere, const vector3& point) noexcept {
    return length_sq(point - input_sphere.center) <=
           input_sphere.radius * input_sphere.radius;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const aabb& box, const vector3& point) noexcept {
    if (box.is_empty()) return false;
    const vector3 offset = point - box.center;
    return detail::abs_scalar(offset.x) <= box.extents.x &&
           detail::abs_scalar(offset.y) <= box.extents.y &&
           detail::abs_scalar(offset.z) <= box.extents.z;
}

// Two spheres overlap when their centres are closer than the radii sum.
// Compared squared, so no square root is needed.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const sphere& x, const sphere& y) noexcept {
    const float reach = x.radius + y.radius;
    return length_sq(y.center - x.center) <= reach * reach;
}

// Per-axis overlap, which is the whole test for two axis-aligned boxes.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const aabb& x, const aabb& y) noexcept {
    if (x.is_empty() || y.is_empty()) return false;
    const vector3 offset = y.center - x.center;
    return detail::abs_scalar(offset.x) <= x.extents.x + y.extents.x &&
           detail::abs_scalar(offset.y) <= x.extents.y + y.extents.y &&
           detail::abs_scalar(offset.z) <= x.extents.z + y.extents.z;
}

// The sphere reaches the box exactly when the box's nearest point is within
// the radius. That one identity is the entire test, and it is why closest_point
// is a named function.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const aabb& box, const sphere& input_sphere) noexcept {
    if (box.is_empty()) return false;
    const vector3 nearest = closest_point(box, input_sphere.center);
    return length_sq(input_sphere.center - nearest) <=
           input_sphere.radius * input_sphere.radius;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const sphere& input_sphere, const aabb& box) noexcept {
    return intersects(box, input_sphere);
}

// Contains(a, b) asks whether b is entirely inside a -- see the note on
// containment in bounds.hpp, because the asymmetry catches people out.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const sphere& outer, const sphere& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;
    // Inside means the far side of the inner sphere still fits.
    const float distance = length(inner.center - outer.center);
    return (distance + inner.radius <= outer.radius) ? containment::contains
                                                     : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const aabb& outer, const aabb& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;

    const vector3 offset = inner.center - outer.center;
    const bool inside =
        detail::abs_scalar(offset.x) + inner.extents.x <= outer.extents.x &&
        detail::abs_scalar(offset.y) + inner.extents.y <= outer.extents.y &&
        detail::abs_scalar(offset.z) + inner.extents.z <= outer.extents.z;
    return inside ? containment::contains : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const aabb& box, const sphere& input_sphere) noexcept {
    if (!intersects(box, input_sphere)) return containment::disjoint;

    const vector3 offset = input_sphere.center - box.center;
    const bool inside =
        detail::abs_scalar(offset.x) + input_sphere.radius <= box.extents.x &&
        detail::abs_scalar(offset.y) + input_sphere.radius <= box.extents.y &&
        detail::abs_scalar(offset.z) + input_sphere.radius <= box.extents.z;
    return inside ? containment::contains : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const sphere& input_sphere, const aabb& box) noexcept {
    if (!intersects(input_sphere, box)) return containment::disjoint;

    // Every corner inside means the whole box is, since a sphere is convex.
    // The farthest corner alone decides it: it is the one at maximum extent in
    // the direction away from the centre.
    const vector3 offset = box.center - input_sphere.center;
    const vector3 farthest{
        detail::abs_scalar(offset.x) + box.extents.x,
        detail::abs_scalar(offset.y) + box.extents.y,
        detail::abs_scalar(offset.z) + box.extents.z};
    return (length_sq(farthest) <= input_sphere.radius * input_sphere.radius)
               ? containment::contains
               : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const sphere& input_sphere, const vector3& point) noexcept {
    return intersects(input_sphere, point) ? containment::contains
                                           : containment::disjoint;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const aabb& box, const vector3& point) noexcept {
    return intersects(box, point) ? containment::contains
                                  : containment::disjoint;
}

// -------------------------------------------------------------- versus plane
// Front means the whole volume sits on the side the normal points to. These
// assume a unit normal -- see plane.hpp; with an unnormalized one the radius
// and the distance are in different units and the comparison is meaningless.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane_side
classify(const sphere& input_sphere, const plane& input_plane) noexcept {
    const float distance = signed_distance(input_plane, input_sphere.center);
    if (distance > input_sphere.radius) return plane_side::front;
    if (distance < -input_sphere.radius) return plane_side::back;
    return plane_side::straddling;
}

// The box's extent along the plane normal is the sum of its half-widths
// projected onto it, which for an axis-aligned box is the dot with the
// normal's absolute value. Comparing that against the centre distance is the
// standard separating-axis test, and it needs no corner enumeration.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane_side
classify(const aabb& box, const plane& input_plane) noexcept {
    // An empty volume has no side. Straddling is conservative for callers that
    // use this result for culling: it cannot incorrectly discard anything.
    if (box.is_empty()) return plane_side::straddling;
    const float reach = box.extents.x * detail::abs_scalar(input_plane.a) +
                        box.extents.y * detail::abs_scalar(input_plane.b) +
                        box.extents.z * detail::abs_scalar(input_plane.c);
    const float distance = signed_distance(input_plane, box.center);
    if (distance > reach) return plane_side::front;
    if (distance < -reach) return plane_side::back;
    return plane_side::straddling;
}

// ------------------------------------------------------------------- raycast
// Every raycast reports the distance along the ray's direction, and a ray that
// starts inside reports zero. See ray.hpp for what that distance is in, which
// depends on whether the direction is normalized.

// Substituting the ray into the sphere equation gives a quadratic in t. The
// linear coefficient is folded in advance (b is half the usual one), which
// removes the factors of two and the 4ac from the discriminant.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
MATHEMATICS_INLINE constexpr bool
raycast(const ray& input_ray, const sphere& input_sphere,
        float& distance_out) noexcept {
    const vector3 to_center = input_ray.origin - input_sphere.center;
    const float a = length_sq(input_ray.direction);
    if (!detail::is_finite_non_zero(a)) return false;

    const float b = dot(to_center, input_ray.direction);
    const float c = length_sq(to_center) -
                    input_sphere.radius * input_sphere.radius;

    // Pointing away with the origin outside: no root can be positive.
    if (c > 0.0f && b > 0.0f) return false;

    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f) return false;

    const float root = detail::scalar_sqrt(discriminant);
    const float t = (-b - root) / a;
    // A negative near root means the origin is inside; the surface is behind,
    // so the hit is at the origin itself.
    distance_out = t < 0.0f ? 0.0f : t;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast(const ray& input_ray, const sphere& input_sphere) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, input_sphere, distance)) return std::nullopt;
    return distance;
}

// The slab method: clip the ray against each pair of parallel faces in turn
// and keep the running entry and exit distances. They cross when the ray
// misses.
//
// Division by a zero direction component is deliberate rather than guarded.
// IEEE-754 gives +/-infinity there, and the comparisons that follow order
// infinities correctly, so a ray exactly parallel to a slab falls out right
// with no branch. The one case that needs care is an origin exactly on a face
// of a zero-width slab, where the division is 0/0 -- that NaN would make every
// comparison false and silently report a miss, so it is checked for.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
MATHEMATICS_INLINE constexpr bool
raycast(const ray& input_ray, const aabb& box, float& distance_out) noexcept {
    if (box.is_empty() ||
        !detail::is_finite_non_zero(length_sq(input_ray.direction))) {
        return false;
    }
    const vector3 minimum = box.min();
    const vector3 maximum = box.max();

    float entry = 0.0f;                        // a ray starting inside hits at 0
    float exit = consteval_ops::infinity;

    const float origin[3] = {input_ray.origin.x, input_ray.origin.y,
                             input_ray.origin.z};
    const float direction[3] = {input_ray.direction.x, input_ray.direction.y,
                                input_ray.direction.z};
    const float low[3] = {minimum.x, minimum.y, minimum.z};
    const float high[3] = {maximum.x, maximum.y, maximum.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0.0f) {
            // Parallel to this slab: either always inside it or never.
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return false;
            }
            continue;
        }
        const float inverse = 1.0f / direction[axis];
        float near = (low[axis] - origin[axis]) * inverse;
        float far = (high[axis] - origin[axis]) * inverse;
        if (near > far) {
            const float swap = near;
            near = far;
            far = swap;
        }
        if (near > entry) entry = near;
        if (far < exit) exit = far;
        if (entry > exit) return false;
    }

    distance_out = entry;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast(const ray& input_ray, const aabb& box) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, box, distance)) return std::nullopt;
    return distance;
}

// A plane is two-sided here: a ray hits it from either face. It misses only
// when it runs parallel, and a ray lying exactly in the plane counts as a miss
// rather than an infinity of hits -- there is no single distance to report.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
MATHEMATICS_INLINE constexpr bool
raycast(const ray& input_ray, const plane& input_plane,
        float& distance_out) noexcept {
    const float slope = dot_normal(input_plane, input_ray.direction);
    if (slope == 0.0f) return false;

    const float t = -signed_distance(input_plane, input_ray.origin) / slope;
    if (t < 0.0f) return false;   // the plane is behind the origin

    distance_out = t;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast(const ray& input_ray, const plane& input_plane) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, input_plane, distance)) return std::nullopt;
    return distance;
}

// Moeller-Trumbore, which finds the barycentric coordinates and the distance
// without ever building the triangle's plane. Single-sided is a rendering
// choice, not a geometric one, so this hits from both faces and leaves the
// winding test to the caller who knows their convention.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast_triangle returns true")
MATHEMATICS_INLINE constexpr bool
raycast_triangle(const ray& input_ray, const vector3& v0, const vector3& v1,
                const vector3& v2, float& distance_out) noexcept {
    const vector3 edge1 = v1 - v0;
    const vector3 edge2 = v2 - v0;
    const vector3 pvec = cross(input_ray.direction, edge2);
    const float determinant = dot(edge1, pvec);

    // Zero determinant means the ray is parallel to the triangle's plane.
    if (!detail::is_finite_non_zero(determinant)) return false;

    const float inverse = 1.0f / determinant;
    const vector3 tvec = input_ray.origin - v0;
    const float u = dot(tvec, pvec) * inverse;
    if (u < 0.0f || u > 1.0f) return false;

    const vector3 qvec = cross(tvec, edge1);
    const float v = dot(input_ray.direction, qvec) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = dot(edge2, qvec) * inverse;
    if (t < 0.0f) return false;

    distance_out = t;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast_triangle(const ray& input_ray, const vector3& v0, const vector3& v1,
                 const vector3& v2) noexcept {
    float distance = 0.0f;
    if (!raycast_triangle(input_ray, v0, v1, v2, distance)) return std::nullopt;
    return distance;
}

// ----------------------------------------------------- bounding frustum helpers
namespace detail {

template <std::size_t count>
inline constexpr void
project_points(const std::array<vector3, count>& points, const vector3& axis,
               float& minimum, float& maximum) noexcept {
    minimum = maximum = dot(points[0], axis);
    for (std::size_t i = 1; i < count; ++i) {
        const float projection = dot(points[i], axis);
        if (projection < minimum) minimum = projection;
        if (projection > maximum) maximum = projection;
    }
}

template <std::size_t count_a, std::size_t count_b>
MATHEMATICS_NODISCARD inline constexpr bool
separated_on_axis(const std::array<vector3, count_a>& a,
                  const std::array<vector3, count_b>& b,
                  const vector3& axis) noexcept {
    // A zero cross product is a redundant SAT axis. Projecting onto it gives
    // [0,0] for both volumes, which naturally reports no separation; the
    // explicit guard also prevents NaN from invalid input leaking through.
    const float axis_length_sq = length_sq(axis);
    if (!is_finite_non_zero(axis_length_sq)) return false;

    float minimum_a = 0.0f, maximum_a = 0.0f;
    float minimum_b = 0.0f, maximum_b = 0.0f;
    project_points(a, axis, minimum_a, maximum_a);
    project_points(b, axis, minimum_b, maximum_b);
    return maximum_a < minimum_b || maximum_b < minimum_a;
}

MATHEMATICS_NODISCARD inline constexpr
std::array<vector3, 8> aabb_corners(const aabb& box) noexcept {
    return {box.corner(0), box.corner(1), box.corner(2), box.corner(3),
            box.corner(4), box.corner(5), box.corner(6), box.corner(7)};
}

// Six unique edge directions: the four perspective rays, plus one horizontal
// and one vertical edge of a depth slice. The remaining twelve physical edges
// are parallel to one of these and add no SAT axis.
MATHEMATICS_NODISCARD inline constexpr
std::array<vector3, 6>
frustum_edge_directions(const std::array<vector3, 8>& corners) noexcept {
    return {corners[4] - corners[0], corners[5] - corners[1],
            corners[6] - corners[2], corners[7] - corners[3],
            corners[1] - corners[0], corners[3] - corners[0]};
}

MATHEMATICS_NODISCARD inline constexpr float
point_segment_distance_sq(const vector3& point, const vector3& a,
                          const vector3& b) noexcept {
    const vector3 edge = b - a;
    const float denominator = length_sq(edge);
    if (!is_finite_non_zero(denominator)) return length_sq(point - a);
    float t = dot(point - a, edge) / denominator;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return length_sq(point - (a + edge * t));
}

// Squared distance to a triangle from Real-Time Collision Detection. The
// degenerate fallback matters for frusta whose near distance is zero: their
// four near corners collapse to the projection origin.
MATHEMATICS_NODISCARD inline constexpr float
point_triangle_distance_sq(const vector3& point, const vector3& a,
                           const vector3& b, const vector3& c) noexcept {
    const vector3 ab = b - a;
    const vector3 ac = c - a;
    if (!is_finite_non_zero(length_sq(cross(ab, ac)))) {
        const float d0 = point_segment_distance_sq(point, a, b);
        const float d1 = point_segment_distance_sq(point, b, c);
        const float d2 = point_segment_distance_sq(point, c, a);
        const float minimum01 = d0 < d1 ? d0 : d1;
        return minimum01 < d2 ? minimum01 : d2;
    }

    const vector3 ap = point - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return length_sq(ap);

    const vector3 bp = point - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return length_sq(bp);

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return length_sq(point - (a + ab * v));
    }

    const vector3 cp = point - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return length_sq(cp);

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return length_sq(point - (a + ac * w));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f) {
        const vector3 bc = c - b;
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return length_sq(point - (b + bc * w));
    }

    const float inverse = 1.0f / (va + vb + vc);
    const float v = vb * inverse;
    const float w = vc * inverse;
    return length_sq(point - (a + ab * v + ac * w));
}

} // namespace detail

// ----------------------------------------------------- frustum point and sphere
MATHEMATICS_NODISCARD inline constexpr bool
intersects(const bounding_frustum& frustum, const vector3& point) noexcept {
    const auto planes = frustum_planes(frustum);
    for (const plane& boundary : planes) {
        if (signed_distance(boundary, point) > 0.0f) return false;
    }
    return true;
}

MATHEMATICS_NODISCARD inline constexpr containment
contains(const bounding_frustum& frustum, const vector3& point) noexcept {
    return intersects(frustum, point) ? containment::contains
                                      : containment::disjoint;
}

MATHEMATICS_NODISCARD inline constexpr bool
intersects(const bounding_frustum& frustum,
           const sphere& input_sphere) noexcept {
    const auto planes = frustum_planes(frustum);
    bool center_inside = true;
    for (const plane& boundary : planes) {
        const float distance = signed_distance(boundary, input_sphere.center);
        if (distance > input_sphere.radius) return false;
        if (distance > 0.0f) center_inside = false;
    }
    if (center_inside) return true;

    // Plane-radius rejection alone has false positives beside edges and
    // corners. The twelve face triangles include all those features, so the
    // nearest surface point is found exactly.
    const auto corners = frustum.corners();
    constexpr std::size_t triangles[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
        {1, 5, 6}, {1, 6, 2}, {0, 3, 7}, {0, 7, 4},
        {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7}};
    const float radius_sq = input_sphere.radius * input_sphere.radius;
    for (const auto& triangle : triangles) {
        if (detail::point_triangle_distance_sq(
                input_sphere.center, corners[triangle[0]],
                corners[triangle[1]], corners[triangle[2]]) <= radius_sq) {
            return true;
        }
    }
    return false;
}

MATHEMATICS_NODISCARD inline constexpr bool
intersects(const sphere& input_sphere,
           const bounding_frustum& frustum) noexcept {
    return intersects(frustum, input_sphere);
}

MATHEMATICS_NODISCARD inline constexpr containment
contains(const bounding_frustum& frustum,
         const sphere& input_sphere) noexcept {
    if (!intersects(frustum, input_sphere)) return containment::disjoint;
    for (const plane& boundary : frustum_planes(frustum)) {
        if (signed_distance(boundary, input_sphere.center) >
            -input_sphere.radius) {
            return containment::intersects;
        }
    }
    return containment::contains;
}

// ------------------------------------------------------- frustum versus AABB
MATHEMATICS_NODISCARD inline constexpr bool
intersects(const bounding_frustum& frustum, const aabb& box) noexcept {
    if (box.is_empty()) return false;
    const auto frustum_corners = frustum.corners();
    const auto box_corners = detail::aabb_corners(box);

    // Face normals from both shapes.
    for (const plane& boundary : frustum_planes(frustum)) {
        if (detail::separated_on_axis(
                frustum_corners, box_corners, boundary.normal())) {
            return false;
        }
    }
    constexpr std::array<vector3, 3> box_axes{
        vector3{1.0f, 0.0f, 0.0f}, vector3{0.0f, 1.0f, 0.0f},
        vector3{0.0f, 0.0f, 1.0f}};
    for (const vector3& axis : box_axes) {
        if (detail::separated_on_axis(frustum_corners, box_corners, axis)) {
            return false;
        }
    }

    // Edge cross products complete the exact convex-polyhedron SAT.
    for (const vector3& frustum_edge :
         detail::frustum_edge_directions(frustum_corners)) {
        for (const vector3& box_axis : box_axes) {
            if (detail::separated_on_axis(
                    frustum_corners, box_corners,
                    cross(frustum_edge, box_axis))) {
                return false;
            }
        }
    }
    return true;
}

MATHEMATICS_NODISCARD inline constexpr bool
intersects(const aabb& box, const bounding_frustum& frustum) noexcept {
    return intersects(frustum, box);
}

MATHEMATICS_NODISCARD inline constexpr containment
contains(const bounding_frustum& frustum, const aabb& box) noexcept {
    if (!intersects(frustum, box)) return containment::disjoint;
    for (const vector3& corner : detail::aabb_corners(box)) {
        if (!intersects(frustum, corner)) return containment::intersects;
    }
    return containment::contains;
}

// ---------------------------------------------------- frustum versus frustum
MATHEMATICS_NODISCARD inline constexpr bool
intersects(const bounding_frustum& x, const bounding_frustum& y) noexcept {
    const auto corners_x = x.corners();
    const auto corners_y = y.corners();

    for (const plane& boundary : frustum_planes(x)) {
        if (detail::separated_on_axis(corners_x, corners_y,
                                      boundary.normal())) {
            return false;
        }
    }
    for (const plane& boundary : frustum_planes(y)) {
        if (detail::separated_on_axis(corners_x, corners_y,
                                      boundary.normal())) {
            return false;
        }
    }

    const auto edges_x = detail::frustum_edge_directions(corners_x);
    const auto edges_y = detail::frustum_edge_directions(corners_y);
    for (const vector3& edge_x : edges_x) {
        for (const vector3& edge_y : edges_y) {
            if (detail::separated_on_axis(
                    corners_x, corners_y, cross(edge_x, edge_y))) {
                return false;
            }
        }
    }
    return true;
}

MATHEMATICS_NODISCARD inline constexpr containment
contains(const bounding_frustum& outer,
         const bounding_frustum& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;
    for (const vector3& corner : inner.corners()) {
        if (!intersects(outer, corner)) return containment::intersects;
    }
    return containment::contains;
}

// Reverse containment overloads mirror DirectXCollision's volume API.
MATHEMATICS_NODISCARD inline constexpr containment
contains(const sphere& outer, const bounding_frustum& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;
    for (const vector3& corner : inner.corners()) {
        if (!intersects(outer, corner)) return containment::intersects;
    }
    return containment::contains;
}

MATHEMATICS_NODISCARD inline constexpr containment
contains(const aabb& outer, const bounding_frustum& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;
    for (const vector3& corner : inner.corners()) {
        if (!intersects(outer, corner)) return containment::intersects;
    }
    return containment::contains;
}

// ----------------------------------------------------------- plane and ray
MATHEMATICS_NODISCARD inline constexpr plane_side
classify(const bounding_frustum& frustum,
         const plane& input_plane) noexcept {
    bool any_front = false;
    bool any_back = false;
    for (const vector3& corner : frustum.corners()) {
        const float distance = signed_distance(input_plane, corner);
        if (distance > 0.0f) any_front = true;
        else if (distance < 0.0f) any_back = true;
        else return plane_side::straddling;
        if (any_front && any_back) return plane_side::straddling;
    }
    return any_front ? plane_side::front : plane_side::back;
}

MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
inline constexpr bool
raycast(const ray& input_ray, const bounding_frustum& frustum,
        float& distance_out) noexcept {
    if (!detail::is_finite_non_zero(length_sq(input_ray.direction))) {
        return false;
    }
    if (intersects(frustum, input_ray.origin)) {
        distance_out = 0.0f;
        return true;
    }

    float entry = 0.0f;
    float exit = consteval_ops::infinity;
    for (const plane& boundary : frustum_planes(frustum)) {
        const float origin_distance =
            signed_distance(boundary, input_ray.origin);
        const float direction_dot =
            dot_normal(boundary, input_ray.direction);
        if (direction_dot == 0.0f) {
            if (origin_distance > 0.0f) return false;
            continue;
        }

        const float t = -origin_distance / direction_dot;
        if (direction_dot < 0.0f) {
            if (t > entry) entry = t;
        } else {
            if (t < exit) exit = t;
        }
        if (entry > exit) return false;
    }

    if (exit < 0.0f) return false;
    distance_out = entry;
    return true;
}

MATHEMATICS_NODISCARD inline constexpr std::optional<float>
raycast(const ray& input_ray, const bounding_frustum& frustum) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, frustum, distance)) return std::nullopt;
    return distance;
}

} // namespace math

#endif // MATHEMATICS_INTERSECT_HPP
