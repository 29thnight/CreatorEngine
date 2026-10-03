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
