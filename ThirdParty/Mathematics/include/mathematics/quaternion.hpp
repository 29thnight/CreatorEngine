// mathematics/quaternion.hpp — unit quaternion rotation, DirectXMath conventions.
//
// Four conventions, all of them load-bearing and all of them observed from
// DirectXMath rather than assumed:
//
//   * Storage is (x, y, z, w) with the scalar LAST, so a quaternion has the same
//     memory layout as a vector4 and the same load and store instructions.
//   * Axis-angle is (axis * sin(theta/2), cos(theta/2)).
//   * `a * b` is the rotation a FOLLOWED BY b. This is the reverse of the
//     textbook Hamilton product, which writes that composition `b * a`. It is
//     not an error: it makes quaternion composition read left to right in
//     application order, exactly like `v * M` and `S * R * T` do for the
//     matrices, so `rotation_matrix(a * b) == rotation_matrix(a) * rotation_matrix(b)`
//     with no reversal anywhere. Pick one order and every layer agrees; pick the
//     textbook one and the matrix layer has to flip.
//   * Rotation is right-handed about the axis, and a rotation matrix built from
//     a quaternion multiplies row vectors on the left.
//
// A quaternion is deliberately NOT a vector_like. vector2/3/4 share their
// arithmetic through that concept, and `*` there is component-wise, following
// HLSL. Component-wise multiplication of two quaternions is meaningless, and
// silently getting it instead of the Hamilton product would be the worst kind of
// bug -- one that type-checks and renders almost right. Leaving `lane_count`
// undefined is what keeps the concept from matching.
#ifndef MATHEMATICS_QUATERNION_HPP
#define MATHEMATICS_QUATERNION_HPP

#include <mathematics/matrix.hpp>
#include <mathematics/scalar.hpp>
#include <mathematics/vector.hpp>

namespace math {

struct quaternion {
    float x, y, z, w;

    // The identity rotation, not the zero quaternion. Zero is not a rotation at
    // all -- it normalizes to nothing and turns any composition into garbage --
    // so a default-constructed quaternion being usable matters more here than
    // the symmetry with vector4's zero.
    constexpr quaternion() noexcept : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

    constexpr quaternion(float x_in, float y_in, float z_in, float w_in) noexcept
        : x(x_in), y(y_in), z(z_in), w(w_in) {}

    // ------------------------------------------------------------ conversions
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg reg() const noexcept {
        MATHEMATICS_IF_CONSTEVAL { return set(x, y, z, w); }
        return load(&x);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static constexpr quaternion
    from_reg(vec_reg r) noexcept {
        MATHEMATICS_IF_CONSTEVAL {
            return quaternion{lane(r, 0), lane(r, 1), lane(r, 2), lane(r, 3)};
        }
        quaternion out;
        store(&out.x, r);
        return out;
    }

    // The vector part, which is the rotation axis scaled by sin(theta/2).
    MATHEMATICS_NODISCARD constexpr vector3 axis() const noexcept {
        return vector3{x, y, z};
    }

    MATHEMATICS_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    MATHEMATICS_NODISCARD static constexpr quaternion identity() noexcept {
        return quaternion{0.0f, 0.0f, 0.0f, 1.0f};
    }
};

static_assert(sizeof(quaternion) == 16, "quaternion must stay packed");
static_assert(std::is_standard_layout_v<quaternion>);
static_assert(std::is_trivially_copyable_v<quaternion>);

// The guard described in the header comment. If quaternion ever satisfies
// vector_like, the component-wise `operator*` template becomes a candidate for
// `q1 * q2` and the Hamilton product below is silently ambiguous or shadowed.
static_assert(!vector_like<quaternion>,
              "quaternion must not satisfy vector_like -- component-wise "
              "operator* would compete with the Hamilton product");

// ------------------------------------------------------------------ construction
// Right-handed about the axis: with a unit +Z axis this sends +X toward +Y.
// The axis is normalized here, so a caller passing a scaled direction gets the
// rotation they meant rather than a quaternion of the wrong magnitude.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
quaternion_from_axis_angle(const vector3& axis, float radians) noexcept {
    const float length_sq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (length_sq == 0.0f) return quaternion::identity();

    const float inv_length = 1.0f / detail::scalar_sqrt(length_sq);
    float s = 0.0f, c = 0.0f;
    sin_cos(radians * 0.5f, s, c);
    const float k = s * inv_length;
    return quaternion{axis.x * k, axis.y * k, axis.z * k, c};
}

// Half-angle sine and cosine per axis, combined in the order DirectXMath's
// XMQuaternionRotationRollPitchYaw uses. That order is roll (Z), then pitch (X),
// then yaw (Y) in application order -- verified against DirectXMath rather than
// derived, because every source states Euler order differently and three of the
// six orders produce plausible-looking results for small angles.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
quaternion_from_pitch_yaw_roll(float pitch, float yaw, float roll) noexcept {
    float sp = 0.0f, cp = 0.0f, sy = 0.0f, cy = 0.0f, sr = 0.0f, cr = 0.0f;
    sin_cos(pitch * 0.5f, sp, cp);
    sin_cos(yaw * 0.5f, sy, cy);
    sin_cos(roll * 0.5f, sr, cr);

    return quaternion{
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        sr * cp * cy - cr * sp * sy,
        cr * cp * cy + sr * sp * sy};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
quaternion_from_euler(const vector3& pitch_yaw_roll) noexcept {
    return quaternion_from_pitch_yaw_roll(pitch_yaw_roll.x, pitch_yaw_roll.y,
                                      pitch_yaw_roll.z);
}

// ------------------------------------------------------------------- products
// The Hamilton product, with the operands swapped relative to the textbook
// formula so that the result is "a, then b" (see the header comment).
//
// Written once, in register operations, rather than as a scalar formula. Those
// operations are already constexpr dual-path -- scalar during constant
// evaluation, intrinsics at run time -- so this is one implementation that
// serves both, with no second version to drift out of step.
//
// The shape is DirectXMath's: each of b's components broadcast against a
// permutation of a, with an alternating sign per row. The signs go in as a
// multiply by +/-1 rather than an XOR of the sign bit, because that is what lets
// the sign and the accumulation fuse into a single multiply-add; the XOR form
// would be one operation cheaper on paper and one instruction longer in fact.
//
// Written as four scalar rows it measured 443 M/s against DirectXMath's 596 --
// outside the +-5% gate. Four lanes is exactly the width where promoting to a
// register pays (docs/PLAN.md Phase 2), unlike the three-component vectors.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
multiply(const quaternion& a, const quaternion& b) noexcept {
    const vec_reg ra = a.reg();
    const vec_reg rb = b.reg();

    //   x =  bw*ax + bx*aw + by*az - bz*ay
    //   y =  bw*ay - bx*az + by*aw + bz*ax
    //   z =  bw*az + bx*ay - by*ax + bz*aw
    //   w =  bw*aw - bx*ax - by*ay - bz*az
    //
    const vec_reg sign_wzyx = set(1.0f, -1.0f, 1.0f, -1.0f);
    const vec_reg sign_zwxy = set(1.0f, 1.0f, -1.0f, -1.0f);
    const vec_reg sign_yxwz = set(-1.0f, 1.0f, 1.0f, -1.0f);

    // Follow the same evolving-shuffle dependency shape as DirectXMath. Besides
    // saving independent permutations, this prevents fast-math reassociation
    // from rebuilding the former three-FMA serial latency chain under Clang.
    vec_reg bx = splat_x(rb);
    vec_reg by = splat_y(rb);
    vec_reg bz = splat_z(rb);
    vec_reg result = mul(splat_w(rb), ra);

    vec_reg shuffled = shuffle<3, 2, 1, 0>(ra); // w z y x
    bx = mul(bx, shuffled);
    shuffled = shuffle<1, 0, 3, 2>(shuffled);  // z w x y
    result = mul_add(bx, sign_wzyx, result);

    by = mul(by, shuffled);
    shuffled = shuffle<3, 2, 1, 0>(shuffled);  // y x w z
    by = mul(by, sign_zwxy);
    bz = mul(bz, shuffled);
    by = mul_add(bz, sign_yxwz, by);
    return quaternion::from_reg(add(result, by));
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
operator*(const quaternion& a, const quaternion& b) noexcept {
    return multiply(a, b);
}

MATHEMATICS_INLINE constexpr quaternion& operator*=(quaternion& a,
                                              const quaternion& b) noexcept {
    return a = multiply(a, b);
}

// Scalar scaling and addition exist for interpolation, not as general
// arithmetic: the result of adding two rotations is not a rotation until it is
// normalized, which is exactly what Nlerp does with them.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
operator*(const quaternion& q, float s) noexcept {
    return quaternion{q.x * s, q.y * s, q.z * s, q.w * s};
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
operator*(float s, const quaternion& q) noexcept {
    return q * s;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
operator+(const quaternion& a, const quaternion& b) noexcept {
    return quaternion{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
operator-(const quaternion& q) noexcept {
    return quaternion{-q.x, -q.y, -q.z, -q.w};
}

// ---------------------------------------------------------------- magnitude
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
dot(const quaternion& a, const quaternion& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
length_sq(const quaternion& q) noexcept {
    return dot(q, q);
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float length(const quaternion& q) noexcept {
    return detail::scalar_sqrt(length_sq(q));
}

// A zero quaternion normalizes to the identity rather than to NaN, matching the
// choice normalize(vector3) makes for the zero vector and for the same reason:
// a NaN here propagates into every transform downstream and is diagnosed a long
// way from where it started.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
normalize(const quaternion& q) noexcept {
    const float squared_length = length_sq(q);
    if (!detail::is_finite_non_zero(squared_length)) return quaternion::identity();
    return q * (1.0f / detail::scalar_sqrt(squared_length));
}

// ------------------------------------------------------------ inverse family
// Negating the vector part. For a unit quaternion this is the inverse, and it is
// what almost every call site actually wants.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
conjugate(const quaternion& q) noexcept {
    return quaternion{-q.x, -q.y, -q.z, q.w};
}

// The true inverse, conjugate over the squared norm, correct for a quaternion
// that has drifted off the unit sphere. Conjugate is the one to reach for when
// the input is known normalized -- this one pays for a division to be right
// about the case where it is not.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
inverse(const quaternion& q) noexcept {
    const float squared_length = length_sq(q);
    if (!detail::is_finite_non_zero(squared_length)) return quaternion::identity();
    return conjugate(q) * (1.0f / squared_length);
}

// -------------------------------------------------------------------- rotate
// v' = v + 2 * cross(qv, cross(qv, v) + w * v), the form that avoids
// materialising the two quaternion products. Same identity DirectXMath uses.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
rotate(const vector3& v, const quaternion& q) noexcept {
    const vector3 qv{q.x, q.y, q.z};
    const vector3 t = cross(qv, v) + v * q.w;
    return v + cross(qv, t) * 2.0f;
}

// The inverse rotation, without building the inverse quaternion first.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
inverse_rotate(const vector3& v, const quaternion& q) noexcept {
    return rotate(v, conjugate(q));
}

// -------------------------------------------------------------- interpolation
// Normalized linear interpolation: cheap, and the right default for animation
// blending where the endpoints are close together. The path is not constant
// speed -- that is what Slerp is for -- but the result is always a unit
// quaternion and the cost is a handful of operations.
//
// Takes the short arc: if the endpoints point into opposite hemispheres they
// describe the same rotations but interpolating between them the long way spins
// nearly all the way round.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
nlerp(const quaternion& a, const quaternion& b, float t) noexcept {
    const float d = dot(a, b);
    const quaternion target = d < 0.0f ? -b : b;
    return normalize(a * (1.0f - t) + target * t);
}

// Constant angular velocity along the shortest arc.
//
// Falls back to Nlerp when the endpoints are nearly parallel: the sine of the
// half-angle is the divisor, and near zero it takes the answer with it. The
// threshold is where the two agree to well within float precision, so the seam
// is invisible.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
slerp(const quaternion& a, const quaternion& b, float t) noexcept {
    float d = dot(a, b);
    const quaternion target = d < 0.0f ? -b : b;
    if (d < 0.0f) d = -d;

    if (d > 0.9995f) return nlerp(a, target, t);

    const float theta = acos(d);
    const float sin_theta = sin(theta);
    const float wa = sin((1.0f - t) * theta) / sin_theta;
    const float wb = sin(t * theta) / sin_theta;
    return a * wa + target * wb;
}

// ------------------------------------------------------------ decomposition
// The axis is undefined for the identity, so it is reported as +X with a zero
// angle rather than as a division by zero.
MATHEMATICS_INLINE constexpr void
to_axis_angle(const quaternion& q, vector3& axis_out, float& radians_out) noexcept {
    const quaternion n = normalize(q);
    // The vector part's length is |sin(theta/2)| and w is cos(theta/2), so the
    // angle comes from an arc tangent of the two rather than an arc cosine of w
    // alone. Near zero and near pi that is the better conditioned of the two.
    const float v_length = detail::scalar_sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (v_length == 0.0f) {
        axis_out = vector3{1.0f, 0.0f, 0.0f};
        radians_out = 0.0f;
        return;
    }
    const float inv = 1.0f / v_length;
    axis_out = vector3{n.x * inv, n.y * inv, n.z * inv};
    radians_out = 2.0f * atan2(v_length, n.w);
}

// Back to pitch (X), yaw (Y), roll (Z), inverting quaternion_from_pitch_yaw_roll.
//
// At the poles -- pitch at +/-90 degrees -- yaw and roll describe the same
// motion and cannot be told apart. That is gimbal lock, and it is a property of
// Euler angles, not a bug to fix: the convention here is to put the whole
// rotation into yaw and leave roll at zero, which is what makes the round trip
// through a quaternion still land on the same rotation.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
to_euler(const quaternion& q) noexcept {
    const quaternion n = normalize(q);
    const float xx = n.x * n.x, yy = n.y * n.y, zz = n.z * n.z;
    const float xy = n.x * n.y, xz = n.x * n.z, yz = n.y * n.z;
    const float wx = n.w * n.x, wy = n.w * n.y, wz = n.w * n.z;

    // Derived from M = Rz(roll) * Rx(pitch) * Ry(yaw), the composition
    // quaternion_from_pitch_yaw_roll builds, read in row-vector form:
    //
    //   M[2][1] = -sin(pitch)                    pitch alone
    //   M[2][0] =  cos(pitch) sin(yaw)           yaw, once pitch is known
    //   M[2][2] =  cos(pitch) cos(yaw)
    //   M[0][1] =  sin(roll)  cos(pitch)         roll likewise
    //   M[1][1] =  cos(roll)  cos(pitch)
    //
    // Substituting the quaternion form of each entry gives the three lines
    // below. Every one of the three signs here was wrong on the first attempt
    // and the round-trip test is what caught it -- a term-by-term derivation is
    // the only way to get this right, because a sign error in yaw or roll still
    // produces a plausible rotation for small angles.
    const float sin_pitch = 2.0f * (wx - yz);

    if (sin_pitch >= 0.99999f || sin_pitch <= -0.99999f) {
        // Gimbal lock. cos(pitch) is zero, so yaw and roll turn about the same
        // axis and only their combination is observable -- no decomposition can
        // recover both. Putting the whole amount in yaw and leaving roll at zero
        // preserves the one thing that can be preserved: feeding the result back
        // through quaternion_from_pitch_yaw_roll lands on the same rotation.
        return vector3{sin_pitch > 0.0f ? half_pi : -half_pi,
                       atan2(2.0f * (wy - xz), 1.0f - 2.0f * (yy + zz)),
                       0.0f};
    }

    return vector3{
        asin(sin_pitch),
        atan2(2.0f * (wy + xz), 1.0f - 2.0f * (xx + yy)),
        atan2(2.0f * (wz + xy), 1.0f - 2.0f * (xx + zz))};
}

// ----------------------------------------------------------- matrix bridging
// Row-vector rotation matrix, matching the rest of the library: the basis
// vectors are the ROWS, so `v * M` and `Rotate(v, q)` agree.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
rotation_matrix(const quaternion& q) noexcept {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    return matrix4x4{
        1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f,
        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f,
        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f,
        0.0f,                    0.0f,                    0.0f,                    1.0f};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix3x3
rotation_matrix3x3(const quaternion& q) noexcept {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    return matrix3x3{
        1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),
        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),
        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy)};
}

// Shepperd's method: pick the largest of the four possible divisors and use its
// branch. The naive `w = sqrt(1 + trace) / 2` loses all precision when the trace
// approaches -1 -- a 180 degree rotation -- and turns into a square root of a
// small negative number a little past it. Choosing the largest component first
// keeps the divisor away from zero in every case.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
quaternion_from_rotation_matrix(const matrix4x4& m) noexcept {
    const float m00 = m.m[0][0], m11 = m.m[1][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;

    if (trace > 0.0f) {
        const float s = detail::scalar_sqrt(trace + 1.0f) * 2.0f;   // 4w
        return quaternion{(m.m[1][2] - m.m[2][1]) / s,
                          (m.m[2][0] - m.m[0][2]) / s,
                          (m.m[0][1] - m.m[1][0]) / s,
                          0.25f * s};
    }
    if (m00 > m11 && m00 > m22) {
        const float s = detail::scalar_sqrt(1.0f + m00 - m11 - m22) * 2.0f;  // 4x
        return quaternion{0.25f * s,
                          (m.m[0][1] + m.m[1][0]) / s,
                          (m.m[2][0] + m.m[0][2]) / s,
                          (m.m[1][2] - m.m[2][1]) / s};
    }
    if (m11 > m22) {
        const float s = detail::scalar_sqrt(1.0f + m11 - m00 - m22) * 2.0f;  // 4y
        return quaternion{(m.m[0][1] + m.m[1][0]) / s,
                          0.25f * s,
                          (m.m[1][2] + m.m[2][1]) / s,
                          (m.m[2][0] - m.m[0][2]) / s};
    }
    const float s = detail::scalar_sqrt(1.0f + m22 - m00 - m11) * 2.0f;      // 4z
    return quaternion{(m.m[2][0] + m.m[0][2]) / s,
                      (m.m[1][2] + m.m[2][1]) / s,
                      0.25f * s,
                      (m.m[0][1] - m.m[1][0]) / s};
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
quaternion_from_rotation_matrix(const matrix3x3& m) noexcept {
    return quaternion_from_rotation_matrix(matrix4x4{
        m.m[0][0], m.m[0][1], m.m[0][2], 0.0f,
        m.m[1][0], m.m[1][1], m.m[1][2], 0.0f,
        m.m[2][0], m.m[2][1], m.m[2][2], 0.0f,
        0.0f,      0.0f,      0.0f,      1.0f});
}

// ------------------------------------------------------------------ comparison
// Exact, component-wise. Note that q and -q are the same rotation but compare
// unequal here -- deliberately, since this compares quaternions, not the
// rotations they stand for. Use Dot to ask the other question: |Dot(a,b)| near
// one means the two describe the same orientation.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const quaternion& a, const quaternion& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const quaternion& a, const quaternion& b,
          float epsilon = 1e-5f) noexcept {
    // Positive test, so a NaN fails rather than passing -- the matrix version of
    // this shipped with the negated form and reported NaN as near-equal.
    const float dx = a.x - b.x, dy = a.y - b.y;
    const float dz = a.z - b.z, dw = a.w - b.w;
    return dx <= epsilon && dx >= -epsilon && dy <= epsilon && dy >= -epsilon &&
           dz <= epsilon && dz >= -epsilon && dw <= epsilon && dw >= -epsilon;
}

// The comparison that usually matters: do these describe the same orientation,
// regardless of which of the two antipodal representations each one uses.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
same_rotation(const quaternion& a, const quaternion& b,
             float epsilon = 1e-5f) noexcept {
    return near_equal(a, b, epsilon) || near_equal(a, -b, epsilon);
}

} // namespace math

#endif // MATHEMATICS_QUATERNION_HPP
