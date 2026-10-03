#pragma once
#include <mathematics/matrix4x4.hpp>
#include "../../FrameCameraSnapshot.h"
#include <array>
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

inline constexpr float kDefaultDistance = 200.f;
struct ReceiverCascade { Sphere bounds; float split{}; };
inline float BlendStart(float split, float band) { return split - std::fabs(split) * band; }

// Shared by pre-budget scene selection and shadow projection. Camera visibility
// and caster relevance must use the same receiver coverage, including overlap.
inline std::array<ReceiverCascade, 3> ReceiverCascades(const FrameCameraSnapshot& camera,
    math::vector3 lightDirection, float distance, float blendBand, unsigned mapSize = 2048)
{
    const auto inverseProjection = math::inverse(camera.projection);
    const float nearPlane = camera.isOrthographic ? camera.nearPlane : (std::max)(camera.nearPlane, .001f);
    const float farPlane = (std::max)(nearPlane + .001f, (std::min)(camera.farPlane, distance));
    std::array<float, 4> splits{nearPlane, 0, 0, farPlane};
    for (unsigned i = 1; i < 3; ++i)
    {
        const float ratio = float(i) / 3.f;
        const float uniform = nearPlane + (farPlane - nearPlane) * ratio;
        splits[i] = camera.isOrthographic ? uniform
            : .75f * nearPlane * std::pow(farPlane / nearPlane, ratio) + .25f * uniform;
    }
    const float xy[4][2]{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    std::array<ReceiverCascade, 3> result;
    for (unsigned i = 0; i < 3; ++i)
    {
        const float from = i == 0 ? nearPlane : (std::max)(nearPlane, BlendStart(splits[i], blendBand));
        std::array<math::vector3, 8> corners;
        math::vector3 center{};
        for (unsigned j = 0; j < 4; ++j)
        {
            corners[j] = math::transform_point(ViewCorner(inverseProjection, xy[j][0], xy[j][1], from), camera.inverseView);
            corners[j + 4] = math::transform_point(ViewCorner(inverseProjection, xy[j][0], xy[j][1], splits[i + 1]), camera.inverseView);
            center += corners[j] + corners[j + 4];
        }
        center /= 8.f;
        float radius = 0;
        for (const auto& corner : corners) radius = (std::max)(radius, math::distance(center, corner));
        radius = std::ceil(radius * 16.f) / 16.f;
        radius *= float(mapSize) / float(mapSize - 2);
        center = SnapCenter(center, lightDirection, radius * 2.f / float(mapSize));
        result[i] = {{center, radius}, splits[i + 1]};
    }
    return result;
}

inline bool CastsInto(const Sphere& receiver, const Sphere& caster, math::vector3 direction)
{
    if (!(caster.radius > 0.f)) return true;
    const auto offset = caster.center - receiver.center;
    const float along = math::dot(offset, direction);
    return along <= receiver.radius + caster.radius
        && math::length(offset - direction * along) <= receiver.radius + caster.radius;
}

inline bool RelevantToView(bool visible, const Sphere& caster,
    const std::array<ReceiverCascade, 3>& receivers, math::vector3 direction, bool hasShadowLight)
{
    if (visible) return true;
    if (!hasShadowLight) return false;
    for (const auto& receiver : receivers)
        if (CastsInto(receiver.bounds, caster, direction)) return true;
    return false;
}
}
