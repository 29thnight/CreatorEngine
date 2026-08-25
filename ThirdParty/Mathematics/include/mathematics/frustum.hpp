// mathematics/frustum.hpp — an oriented perspective bounding frustum.
//
// The representation deliberately matches DirectXCollision::BoundingFrustum:
// an origin and orientation plus four X/Z and Y/Z slopes and two local-space Z
// distances. It is substantially smaller than storing six world planes, cheap
// to rigidly transform, and preserves an asymmetric projection exactly.
//
// A right-handed projection still uses the same representation. Its local Z
// distances and slopes are negative/reversed in exactly the way produced by
// DirectXCollision's rhcoords path; corners and planes remain correct without a
// hidden handedness flag.
#ifndef MATHEMATICS_FRUSTUM_HPP
#define MATHEMATICS_FRUSTUM_HPP

#include <mathematics/bounds.hpp>
#include <mathematics/matrix.hpp>
#include <mathematics/plane.hpp>
#include <mathematics/quaternion.hpp>

#include <array>
#include <optional>

namespace math {

struct bounding_frustum {
    static constexpr std::size_t corner_count = 8;
    static constexpr std::size_t plane_count = 6;

    vector3 origin;
    quaternion orientation;
    float right_slope;
    float left_slope;
    float top_slope;
    float bottom_slope;
    float near_plane;
    float far_plane;

    // The canonical 90-degree LH frustum, matching DirectXCollision's default.
    constexpr bounding_frustum() noexcept
        : origin{0.0f, 0.0f, 0.0f}, orientation{},
          right_slope(1.0f), left_slope(-1.0f),
          top_slope(1.0f), bottom_slope(-1.0f),
          near_plane(0.0f), far_plane(1.0f) {}

    constexpr bounding_frustum(
        const vector3& origin_in, const quaternion& orientation_in,
        float right_slope_in, float left_slope_in,
        float top_slope_in, float bottom_slope_in,
        float near_plane_in, float far_plane_in) noexcept
        : origin(origin_in), orientation(orientation_in),
          right_slope(right_slope_in), left_slope(left_slope_in),
          top_slope(top_slope_in), bottom_slope(bottom_slope_in),
          near_plane(near_plane_in), far_plane(far_plane_in) {}

    // DirectX corner order:
    //
    //     Near    Far
    //    0----1  4----5
    //    |    |  |    |
    //    3----2  7----6
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
    corner(std::size_t index) const noexcept {
        const std::size_t face_index = index & 3u;
        const float distance = index < 4u ? near_plane : far_plane;
        const float slope_x = (face_index == 0u || face_index == 3u)
                                  ? left_slope : right_slope;
        const float slope_y = (face_index == 0u || face_index == 1u)
                                  ? top_slope : bottom_slope;
        const vector3 local{slope_x * distance, slope_y * distance, distance};
        return rotate(local, orientation) + origin;
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr
    std::array<vector3, corner_count> corners() const noexcept {
        return {corner(0), corner(1), corner(2), corner(3),
                corner(4), corner(5), corner(6), corner(7)};
    }
};

static_assert(sizeof(bounding_frustum) == 52,
              "bounding_frustum must match the packed DirectX layout size");
static_assert(std::is_standard_layout_v<bounding_frustum>);
static_assert(std::is_trivially_copyable_v<bounding_frustum>);

namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
is_finite(float value) noexcept {
    return value - value == 0.0f;
}

MATHEMATICS_NODISCARD inline constexpr bool
try_bounding_frustum_from_projection(const matrix4x4& projection,
                                     bool right_handed,
                                     bounding_frustum& result) noexcept {
    // A perspective-only row-vector projection has:
    //
    //   clip.x = x*m00 + z*m20,  clip.w = z*m23
    //   clip.y = y*m11 + z*m21
    //   clip.z = z*m22 + m32
    //
    // Solving those three ratios directly is equivalent to inverse-projecting
    // the six NDC probe points DirectXCollision uses, but avoids evaluating a
    // general 4x4 inverse in every constexpr call. Besides being less work at
    // run time, this cuts MSVC constant-evaluation memory by several gigabytes.
    const float m00 = projection.m[0][0];
    const float m11 = projection.m[1][1];
    const float m20 = projection.m[2][0];
    const float m21 = projection.m[2][1];
    const float m22 = projection.m[2][2];
    const float m23 = projection.m[2][3];
    const float m32 = projection.m[3][2];
    const float far_denominator = m23 - m22;

    if (m00 == 0.0f || m11 == 0.0f || m22 == 0.0f || m23 == 0.0f ||
        m32 == 0.0f || far_denominator == 0.0f) {
        return false;
    }
    if (!is_finite_non_zero(m00) || !is_finite_non_zero(m11) ||
        !is_finite_non_zero(m22) || !is_finite_non_zero(m23) ||
        !is_finite_non_zero(m32) || !is_finite_non_zero(far_denominator)) {
        return false;
    }

    const float near_z = -m32 / m22;
    const float far_z = m32 / far_denominator;
    const bounding_frustum candidate{
        vector3::zero(), quaternion::identity(),
        (m23 - m20) / m00,
        (-m23 - m20) / m00,
        (m23 - m21) / m11,
        (-m23 - m21) / m11,
        right_handed ? far_z : near_z,
        right_handed ? near_z : far_z};

    if (!is_finite(candidate.right_slope) ||
        !is_finite(candidate.left_slope) ||
        !is_finite(candidate.top_slope) ||
        !is_finite(candidate.bottom_slope) ||
        !is_finite(candidate.near_plane) ||
        !is_finite(candidate.far_plane)) {
        return false;
    }

    result = candidate;
    return true;
}

} // namespace detail

MATHEMATICS_NODISCARD inline constexpr
std::optional<bounding_frustum>
try_bounding_frustum_from_projection_lh(const matrix4x4& projection) noexcept {
    bounding_frustum result;
    if (!detail::try_bounding_frustum_from_projection(
            projection, false, result)) {
        return std::nullopt;
    }
    return result;
}

MATHEMATICS_NODISCARD inline constexpr
std::optional<bounding_frustum>
try_bounding_frustum_from_projection_rh(const matrix4x4& projection) noexcept {
    bounding_frustum result;
    if (!detail::try_bounding_frustum_from_projection(
            projection, true, result)) {
        return std::nullopt;
    }
    return result;
}

// Value-returning convenience follows the library's degenerate-input policy:
// an invalid/singular projection returns the usable canonical default. Reach
// for the try_ form when failure must be distinguished from that value.
MATHEMATICS_NODISCARD inline constexpr bounding_frustum
bounding_frustum_from_projection_lh(const matrix4x4& projection) noexcept {
    const auto result = try_bounding_frustum_from_projection_lh(projection);
    return result ? *result : bounding_frustum{};
}

MATHEMATICS_NODISCARD inline constexpr bounding_frustum
bounding_frustum_from_projection_rh(const matrix4x4& projection) noexcept {
    const auto result = try_bounding_frustum_from_projection_rh(projection);
    return result ? *result : bounding_frustum{};
}

// Outward-facing unit planes. The interior is the non-positive half-space for
// every plane, matching DirectXCollision::BoundingFrustum::GetPlanes.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr
std::array<plane, bounding_frustum::plane_count>
frustum_planes(const bounding_frustum& frustum) noexcept {
    const std::array<plane, bounding_frustum::plane_count> local{
        plane{0.0f, 0.0f, -1.0f, frustum.near_plane},
        plane{0.0f, 0.0f, 1.0f, -frustum.far_plane},
        plane{1.0f, 0.0f, -frustum.right_slope, 0.0f},
        plane{-1.0f, 0.0f, frustum.left_slope, 0.0f},
        plane{0.0f, 1.0f, -frustum.top_slope, 0.0f},
        plane{0.0f, -1.0f, frustum.bottom_slope, 0.0f}};

    std::array<plane, bounding_frustum::plane_count> world{};
    for (std::size_t i = 0; i < local.size(); ++i) {
        const vector3 normal = rotate(local[i].normal(), frustum.orientation);
        world[i] = normalize(plane{normal.x, normal.y, normal.z,
                                   local[i].d - dot(normal, frustum.origin)});
    }
    return world;
}

// Uniform scale, rotation, translation in application order. Quaternion
// multiplication follows the library convention, so orientation * rotation is
// the old frustum rotation followed by the supplied world rotation.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bounding_frustum
transform(const bounding_frustum& frustum, float scale,
          const quaternion& rotation, const vector3& translation) noexcept {
    const quaternion unit_rotation = normalize(rotation);
    return bounding_frustum{
        rotate(frustum.origin * scale, unit_rotation) + translation,
        normalize(frustum.orientation * unit_rotation),
        frustum.right_slope, frustum.left_slope,
        frustum.top_slope, frustum.bottom_slope,
        frustum.near_plane * scale, frustum.far_plane * scale};
}

// DirectXCollision-compatible affine transform. Non-uniform scale cannot be
// represented by this compact frustum, so the largest basis scale is used for
// near/far as a conservative bound while the normalized basis supplies the
// rotation. For an ordinary uniform TRS this is exact.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bounding_frustum
transform(const bounding_frustum& frustum, const matrix4x4& matrix) noexcept {
    const vector3 row0{matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]};
    const vector3 row1{matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]};
    const vector3 row2{matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]};
    const float scale0 = length(row0);
    const float scale1 = length(row1);
    const float scale2 = length(row2);
    if (!detail::is_finite_non_zero(scale0) ||
        !detail::is_finite_non_zero(scale1) ||
        !detail::is_finite_non_zero(scale2)) {
        return bounding_frustum{};
    }

    const matrix4x4 rotation_basis{
        row0.x / scale0, row0.y / scale0, row0.z / scale0, 0.0f,
        row1.x / scale1, row1.y / scale1, row1.z / scale1, 0.0f,
        row2.x / scale2, row2.y / scale2, row2.z / scale2, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const quaternion rotation =
        normalize(quaternion_from_rotation_matrix(rotation_basis));
    const float maximum12 = scale1 > scale2 ? scale1 : scale2;
    const float maximum_scale = scale0 > maximum12 ? scale0 : maximum12;

    return bounding_frustum{
        transform_point(frustum.origin, matrix),
        normalize(frustum.orientation * rotation),
        frustum.right_slope, frustum.left_slope,
        frustum.top_slope, frustum.bottom_slope,
        frustum.near_plane * maximum_scale,
        frustum.far_plane * maximum_scale};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
bounding_box(const bounding_frustum& frustum) noexcept {
    return aabb_from_points(frustum.corners());
}

// Conservative, not minimum: the sphere around the tight AABB is stable and
// guaranteed to contain all eight corners.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr sphere
bounding_sphere(const bounding_frustum& frustum) noexcept {
    return bounding_sphere(bounding_box(frustum));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const bounding_frustum& x, const bounding_frustum& y) noexcept {
    return x.origin == y.origin && x.orientation == y.orientation &&
           x.right_slope == y.right_slope && x.left_slope == y.left_slope &&
           x.top_slope == y.top_slope && x.bottom_slope == y.bottom_slope &&
           x.near_plane == y.near_plane && x.far_plane == y.far_plane;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const bounding_frustum& x, const bounding_frustum& y,
           float epsilon = 1e-5f) noexcept {
    const float dr = x.right_slope - y.right_slope;
    const float dl = x.left_slope - y.left_slope;
    const float dt = x.top_slope - y.top_slope;
    const float db = x.bottom_slope - y.bottom_slope;
    const float dn = x.near_plane - y.near_plane;
    const float df = x.far_plane - y.far_plane;
    return near_equal(x.origin, y.origin, epsilon) &&
           same_rotation(x.orientation, y.orientation, epsilon) &&
           dr <= epsilon && dr >= -epsilon &&
           dl <= epsilon && dl >= -epsilon &&
           dt <= epsilon && dt >= -epsilon &&
           db <= epsilon && db >= -epsilon &&
           dn <= epsilon && dn >= -epsilon &&
           df <= epsilon && df >= -epsilon;
}

} // namespace math

#endif // MATHEMATICS_FRUSTUM_HPP
