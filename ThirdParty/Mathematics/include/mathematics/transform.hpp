// mathematics/transform.hpp — transform, view and projection matrices.
//
// Everything here follows the conventions the rest of the library already
// committed to: row-major storage, row vectors, composition left to right in
// application order, translation in row 3.
//
// HANDEDNESS IS ALWAYS IN THE NAME. There is no LookAt, only look_at_lh and
// look_at_rh. A default would be a coin flip that silently mirrors a scene, and
// the compiler cannot catch it -- everything still builds, renders, and looks
// almost right until something reads a normal or a winding order. DirectXMath
// makes the same choice, and the numbers below are matched against it.
//
// The depth convention is Direct3D's: clip z runs from 0 at the near plane to 1
// at the far plane, not OpenGL's -1 to 1. That is what the LH and RH pairs here
// produce, in both the perspective and the orthographic forms.
#ifndef MATHEMATICS_TRANSFORM_HPP
#define MATHEMATICS_TRANSFORM_HPP

#include <mathematics/bounds.hpp>
#include <mathematics/matrix.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/scalar.hpp>
#include <mathematics/vector.hpp>

#include <optional>

namespace math {

// ------------------------------------------------------------ basic transforms
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
scaling_matrix(const vector3& s) noexcept {
    return matrix4x4{s.x, 0, 0, 0,
                     0, s.y, 0, 0,
                     0, 0, s.z, 0,
                     0, 0, 0,   1};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
scaling_matrix(float s) noexcept {
    return scaling_matrix(vector3{s, s, s});
}

// Row 3, per the convention -- not column 3.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
translation_matrix(const vector3& t) noexcept {
    return matrix4x4{1, 0, 0, 0,
                     0, 1, 0, 0,
                     0, 0, 1, 0,
                     t.x, t.y, t.z, 1};
}

// Right-handed about each axis, matching quaternion_from_axis_angle: rotation_z
// sends +X toward +Y. In row-vector form that puts the positive sine above the
// diagonal, which is the transpose of the arrangement most textbooks print.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
rotation_x(float radians) noexcept {
    float s = 0.0f, c = 0.0f;
    sin_cos(radians, s, c);
    return matrix4x4{1, 0, 0, 0,
                     0, c, s, 0,
                     0, -s, c, 0,
                     0, 0, 0, 1};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
rotation_y(float radians) noexcept {
    float s = 0.0f, c = 0.0f;
    sin_cos(radians, s, c);
    return matrix4x4{c, 0, -s, 0,
                     0, 1, 0, 0,
                     s, 0, c, 0,
                     0, 0, 0, 1};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
rotation_z(float radians) noexcept {
    float s = 0.0f, c = 0.0f;
    sin_cos(radians, s, c);
    return matrix4x4{c, s, 0, 0,
                     -s, c, 0, 0,
                     0, 0, 1, 0,
                     0, 0, 0, 1};
}

// ------------------------------------------------------------------ TRS
// Scale, then rotate, then translate -- the order that reads left to right and
// the only one that behaves the way a scene graph expects, since it leaves the
// translation unscaled and unrotated.
//
// Built directly rather than as `scaling_matrix(s) * rotation_matrix(r) *
// translation_matrix(t)`: those two products are almost all multiplications by
// zero and one, and writing the result out skips both of them.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
compose(const vector3& scale, const quaternion& rotation,
        const vector3& translation) noexcept {
    matrix4x4 m = rotation_matrix(rotation);

    m.m[0][0] *= scale.x; m.m[0][1] *= scale.x; m.m[0][2] *= scale.x;
    m.m[1][0] *= scale.y; m.m[1][1] *= scale.y; m.m[1][2] *= scale.y;
    m.m[2][0] *= scale.z; m.m[2][1] *= scale.z; m.m[2][2] *= scale.z;

    m.m[3][0] = translation.x;
    m.m[3][1] = translation.y;
    m.m[3][2] = translation.z;
    return m;
}

// ---------------------------------------------------------- AABB affine transform
// An AABB cannot retain an orientation. Transform its centre as a point, then
// project its three half-width vectors onto each world axis. Taking the absolute
// value of the linear rows produces the same tight world AABB as transforming
// all eight corners, with three multiply-add chains instead of eight point
// transforms plus a min/max reduction.
//
// This is an affine operation: perspective projection matrices are outside the
// contract because no homogeneous divide is performed.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
transform(const aabb& box, const matrix4x4& matrix) noexcept {
    if (box.is_empty()) return box;

    const vec_reg row0 = matrix.row(0);
    const vec_reg row1 = matrix.row(1);
    const vec_reg row2 = matrix.row(2);
    const vec_reg center = box.center.reg();
    const vec_reg extents = box.extents.reg();

    vec_reg transformed_center = mul(splat_x(center), row0);
    transformed_center =
        mul_add(splat_y(center), row1, transformed_center);
    transformed_center =
        mul_add(splat_z(center), row2, transformed_center);
    transformed_center = add(transformed_center, matrix.row(3));

    vec_reg transformed_extents = mul(splat_x(extents), abs(row0));
    transformed_extents =
        mul_add(splat_y(extents), abs(row1), transformed_extents);
    transformed_extents =
        mul_add(splat_z(extents), abs(row2), transformed_extents);

    return aabb{vector3::from_reg(transformed_center),
                vector3::from_reg(transformed_extents)};
}

// DirectXCollision BoundingBox::Transform-compatible convenience form. The
// quaternion is normalized so non-unit caller input follows the library's safe
// rotation policy; unit input is numerically equivalent to the DirectX path.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr aabb
transform(const aabb& box, float scale, const quaternion& rotation,
          const vector3& translation) noexcept {
    return transform(box,
                     compose(vector3{scale, scale, scale}, normalize(rotation),
                             translation));
}

struct decomposed_transform {
    vector3 scale;
    quaternion rotation;
    vector3 translation;
};

// Splits an affine transform back into scale, rotation and translation.
//
// Returns false, and leaves the outputs untouched, when the matrix has no such
// decomposition: a zero or near-zero scale on any axis destroys the direction of
// that basis vector, and no rotation can be recovered from it.
//
// A negative determinant means the transform includes a reflection, which no
// combination of scale and rotation can express -- a rotation matrix always has
// determinant +1. The convention here is to fold the reflection into a negative
// X scale, which is what DirectXMath and most engines do. It is a choice, not a
// recovery: the original might have had the mirror on Y or Z, and nothing in the
// matrix records which.
//
// Skew is not representable either, and unlike reflection it is not detected --
// a sheared matrix decomposes into something that is not equal to it. Compose
// and Decompose round-trip exactly for matrices Compose could have produced.
MATHEMATICS_NODISCARD_MSG("output values are valid only when decompose returns true")
MATHEMATICS_INLINE constexpr bool
decompose(const matrix4x4& m, vector3& scale_out, quaternion& rotation_out,
          vector3& translation_out) noexcept {
    const vector3 r0{m.m[0][0], m.m[0][1], m.m[0][2]};
    const vector3 r1{m.m[1][0], m.m[1][1], m.m[1][2]};
    const vector3 r2{m.m[2][0], m.m[2][1], m.m[2][2]};

    float sx = length(r0);
    const float sy = length(r1);
    const float sz = length(r2);

    if (!detail::is_finite_non_zero(sx) || !detail::is_finite_non_zero(sy) ||
        !detail::is_finite_non_zero(sz)) {
        return false;
    }

    // Determinant of the upper 3x3. Negative means a reflection is baked in.
    const float det =
        r0.x * (r1.y * r2.z - r1.z * r2.y) -
        r0.y * (r1.x * r2.z - r1.z * r2.x) +
        r0.z * (r1.x * r2.y - r1.y * r2.x);
    if (det < 0.0f) sx = -sx;

    const matrix3x3 basis{
        r0.x / sx, r0.y / sx, r0.z / sx,
        r1.x / sy, r1.y / sy, r1.z / sy,
        r2.x / sz, r2.y / sz, r2.z / sz};

    scale_out = vector3{sx, sy, sz};
    rotation_out = quaternion_from_rotation_matrix(basis);
    translation_out = vector3{m.m[3][0], m.m[3][1], m.m[3][2]};
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr
std::optional<decomposed_transform> decompose(const matrix4x4& m) noexcept {
    decomposed_transform result{};
    if (!decompose(m, result.scale, result.rotation, result.translation)) {
        return std::nullopt;
    }
    return result;
}

// ------------------------------------------------------------------- view
namespace detail {

// The shared body of all four look-at forms. `forward` must already point the
// way the convention wants: toward the target for left-handed, away from it for
// right-handed.
//
// Degenerate input -- a zero direction, or an up vector parallel to it --
// returns the identity, per the library-wide policy (inverse of a singular
// matrix, normalize of a zero vector). Without the guard, cross(up, f) is the
// zero vector, normalize hands the zero back, and the function would return a
// matrix with two zero basis columns: a camera that silently collapses the
// scene to a line, discovered a long way from the LookAt call that caused it.
// Looking straight up with the conventional world up is exactly this case.
// DirectXMath asserts in debug builds and produces the garbage in release;
// returning something usable and well-defined is this library's choice.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
look_to_impl(const vector3& eye, const vector3& forward,
           const vector3& up) noexcept {
    const vector3 f = normalize(forward);
    const vector3 side = cross(up, f);
    if (!detail::is_finite_non_zero(length_sq(side))) return matrix4x4::identity();
    const vector3 r = normalize(side);
    const vector3 u = cross(f, r);

    // The basis goes in as COLUMNS, because a view matrix is the inverse of the
    // camera's transform and the inverse of a rotation is its transpose.
    return matrix4x4{
        r.x, u.x, f.x, 0.0f,
        r.y, u.y, f.y, 0.0f,
        r.z, u.z, f.z, 0.0f,
        -dot(r, eye), -dot(u, eye), -dot(f, eye), 1.0f};
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
look_to_lh(const vector3& eye, const vector3& direction,
         const vector3& up) noexcept {
    return detail::look_to_impl(eye, direction, up);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
look_to_rh(const vector3& eye, const vector3& direction,
         const vector3& up) noexcept {
    return detail::look_to_impl(eye, -direction, up);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
look_at_lh(const vector3& eye, const vector3& target, const vector3& up) noexcept {
    return detail::look_to_impl(eye, target - eye, up);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
look_at_rh(const vector3& eye, const vector3& target, const vector3& up) noexcept {
    return detail::look_to_impl(eye, eye - target, up);
}

// ------------------------------------------------------------- projection
// Clip z is 0 at the near plane and 1 at the far plane (Direct3D), and w carries
// the view-space depth so the perspective divide happens for free.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
perspective_fov_lh(float fov_y, float aspect, float near_z, float far_z) noexcept {
    float s = 0.0f, c = 0.0f;
    sin_cos(fov_y * 0.5f, s, c);
    const float h = c / s;              // cot(fov_y/2)
    const float w = h / aspect;
    const float range = far_z / (far_z - near_z);

    return matrix4x4{w, 0, 0, 0,
                     0, h, 0, 0,
                     0, 0, range, 1,
                     0, 0, -range * near_z, 0};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
perspective_fov_rh(float fov_y, float aspect, float near_z, float far_z) noexcept {
    float s = 0.0f, c = 0.0f;
    sin_cos(fov_y * 0.5f, s, c);
    const float h = c / s;
    const float w = h / aspect;
    const float range = far_z / (near_z - far_z);

    return matrix4x4{w, 0, 0, 0,
                     0, h, 0, 0,
                     0, 0, range, -1,
                     0, 0, range * near_z, 0};
}

// The same projection specified by the view-space size of the near plane rather
// than by a field of view.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
perspective_lh(float width, float height, float near_z, float far_z) noexcept {
    const float two_near = near_z + near_z;
    const float range = far_z / (far_z - near_z);
    return matrix4x4{two_near / width, 0, 0, 0,
                     0, two_near / height, 0, 0,
                     0, 0, range, 1,
                     0, 0, -range * near_z, 0};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
perspective_rh(float width, float height, float near_z, float far_z) noexcept {
    const float two_near = near_z + near_z;
    const float range = far_z / (near_z - far_z);
    return matrix4x4{two_near / width, 0, 0, 0,
                     0, two_near / height, 0, 0,
                     0, 0, range, -1,
                     0, 0, range * near_z, 0};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
orthographic_lh(float width, float height, float near_z, float far_z) noexcept {
    const float range = 1.0f / (far_z - near_z);
    return matrix4x4{2.0f / width, 0, 0, 0,
                     0, 2.0f / height, 0, 0,
                     0, 0, range, 0,
                     0, 0, -range * near_z, 1};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
orthographic_rh(float width, float height, float near_z, float far_z) noexcept {
    const float range = 1.0f / (near_z - far_z);
    return matrix4x4{2.0f / width, 0, 0, 0,
                     0, 2.0f / height, 0, 0,
                     0, 0, range, 0,
                     0, 0, range * near_z, 1};
}

// Asymmetric frustum, for split screens, shadow cascades and tiled rendering.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
orthographic_off_center_lh(float left, float right, float bottom, float top,
                        float near_z, float far_z) noexcept {
    const float rcp_width = 1.0f / (right - left);
    const float rcp_height = 1.0f / (top - bottom);
    const float range = 1.0f / (far_z - near_z);

    return matrix4x4{
        rcp_width + rcp_width, 0, 0, 0,
        0, rcp_height + rcp_height, 0, 0,
        0, 0, range, 0,
        -(left + right) * rcp_width, -(top + bottom) * rcp_height,
        -range * near_z, 1};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
orthographic_off_center_rh(float left, float right, float bottom, float top,
                        float near_z, float far_z) noexcept {
    const float rcp_width = 1.0f / (right - left);
    const float rcp_height = 1.0f / (top - bottom);
    const float range = 1.0f / (near_z - far_z);

    return matrix4x4{
        rcp_width + rcp_width, 0, 0, 0,
        0, rcp_height + rcp_height, 0, 0,
        0, 0, range, 0,
        -(left + right) * rcp_width, -(top + bottom) * rcp_height,
        range * near_z, 1};
}

} // namespace math

#endif // MATHEMATICS_TRANSFORM_HPP
