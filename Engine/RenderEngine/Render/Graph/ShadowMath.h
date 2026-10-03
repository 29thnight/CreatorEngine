#pragma once
#include <mathematics/matrix4x4.hpp>
#include <algorithm>
#include <cmath>

namespace shadow_math
{
struct Sphere { math::vector3 center{}; float radius{}; };

inline bool IntersectsClip(const Sphere& sphere, const math::matrix4x4& matrix)
{
    if (!(sphere.radius > 0.f)) return true; // Unknown bounds must never reject.
    for (unsigned axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1})
        {
            const bool nearPlane = axis == 2 && sign == 1;
            const math::vector3 normal{
                (nearPlane ? 0.f : matrix.m[0][3]) + sign * matrix.m[0][axis],
                (nearPlane ? 0.f : matrix.m[1][3]) + sign * matrix.m[1][axis],
                (nearPlane ? 0.f : matrix.m[2][3]) + sign * matrix.m[2][axis]};
            const float offset = (nearPlane ? 0.f : matrix.m[3][3]) + sign * matrix.m[3][axis];
            if (math::dot(sphere.center, normal) + offset < -sphere.radius * math::length(normal))
                return false;
        }
    return true;
}

// Frobenius norm bounds arbitrary affine scale, including parent-induced shear.
inline float ScaleBound(const math::matrix4x4& matrix)
{
    return std::sqrt(math::length_sq(matrix.right()) + math::length_sq(matrix.up())
        + math::length_sq(matrix.forward()));
}

inline math::vector3 ViewCorner(const math::matrix4x4& inverseProjection,
    float x, float y, float depth)
{
    const auto a = math::vector4{x, y, 0.f, 1.f} * inverseProjection;
    const auto b = math::vector4{x, y, 1.f, 1.f} * inverseProjection;
    const math::vector3 nearPoint{a.x / a.w, a.y / a.w, a.z / a.w};
    const math::vector3 farPoint{b.x / b.w, b.y / b.w, b.z / b.w};
    return nearPoint + (farPoint - nearPoint)
        * ((depth - nearPoint.z) / (farPoint.z - nearPoint.z));
}

inline math::vector3 SnapCenter(math::vector3 center, math::vector3 direction, float texel)
{
    const math::vector3 up = std::fabs(direction.y) > .99f
        ? math::vector3{0, 0, 1} : math::vector3{0, 1, 0};
    const auto right = math::normalize(math::cross(up, direction));
    const auto vertical = math::cross(direction, right);
    const float x = math::dot(center, right), y = math::dot(center, vertical);
    return center + right * (std::round(x / texel) * texel - x)
        + vertical * (std::round(y / texel) * texel - y);
}
}
