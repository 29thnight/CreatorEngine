#pragma once
#include "../Physics/PhysicsGeometry.h"
#include <mathematics/transform.hpp>
#include <mathematics/views.hpp>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <limits>

struct PhysicsTransformState
{
    ce::physics::pose pose;
    math::vector3 scale;
};

inline bool PhysicsScalesNear(math::vector3 a, math::vector3 b)
{
    constexpr float tolerance = 8.f * std::numeric_limits<float>::epsilon();
    const auto nearlyEqual = [](float x, float y) {
        return std::isfinite(x) && std::isfinite(y) &&
            std::abs(x-y) <= tolerance * (std::max)({1.f, std::abs(x), std::abs(y)});
    };

    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
}

inline bool PhysicsTransformsNear(const math::matrix4x4& a, const math::matrix4x4& b,
                                  float tolerance = 8.f * std::numeric_limits<float>::epsilon())
{
    auto left = math::rows(a) | std::views::join;
    auto right = math::rows(b) | std::views::join;
    return std::ranges::all_of(std::views::zip(left, right), [tolerance](const auto& pair) {
        const auto [x, y] = pair;
        return std::isfinite(x) && std::isfinite(y) && std::abs(x-y) <= tolerance * (std::max)(1.f, std::abs(x));
    });
}

// A rigid SDK pose plus shape scale cannot represent shear. Reject it before
// changing the accepted body, including shear introduced by a parent transform.
inline ce::physics::result<PhysicsTransformState> CapturePhysicsTransform(const math::matrix4x4& world)
{
    PhysicsTransformState value;
    if (!math::decompose(world, value.scale, value.pose.rotation, value.pose.position))
        return std::unexpected(ce::physics::error{ce::physics::error_code::invalid_argument, 0, "Physics Transform cannot be decomposed"});

    value.pose.rotation = math::normalize(value.pose.rotation);
    const auto reconstructed = math::compose(value.scale, value.pose.rotation, value.pose.position);
    if (!PhysicsTransformsNear(world, reconstructed, 1e-4f))
        return std::unexpected(ce::physics::error{ce::physics::error_code::invalid_argument, 0, "Physics Transform contains unsupported shear"});

    return value;
}

// Scene transforms are affine. General inversion may round its homogeneous
// component away from one; preserve the affine contract before transporting a
// descendant through the interpolated rigid-body frame.
inline math::matrix4x4 PhysicsRenderDescendantMatrix(const math::matrix4x4& world,
                                                    const math::matrix4x4& bodyWorld,
                                                    const math::matrix4x4& interpolated)
{
    auto inverse = math::try_inverse(bodyWorld);
    if (!inverse)
        return world;

    inverse->m[0][3] = 0.f;
    inverse->m[1][3] = 0.f;
    inverse->m[2][3] = 0.f;
    inverse->m[3][3] = 1.f;

    return world * *inverse * interpolated;
}
