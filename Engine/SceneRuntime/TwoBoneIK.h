#pragma once

#include <mathematics/quaternion.hpp>
#include <mathematics/vector3.hpp>
#include <algorithm>
#include <cmath>

namespace animation
{
    struct two_bone_solution final
    {
        math::vector3 middle{};
        math::vector3 end{};
    };

    [[nodiscard]] inline bool finite(math::vector3 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y)
            && std::isfinite(value.z);
    }

    [[nodiscard]] inline math::vector3 perpendicular(math::vector3 direction) noexcept
    {
        const math::vector3 basis = std::abs(direction.y) < .9f
            ? math::vector3{ 0.f, 1.f, 0.f } : math::vector3{ 1.f, 0.f, 0.f };
        return math::normalize(math::cross(direction, basis));
    }

    // Constrain the target to the chain's reach and preserve its two segment
    // lengths. The pole selects the bend side; a collinear pole falls back to
    // the previous elbow plane so a straight leg does not flip arbitrarily.
    [[nodiscard]] inline bool solve_two_bone_positions(math::vector3 start,
        math::vector3 middle, math::vector3 end, math::vector3 target,
        math::vector3 pole, two_bone_solution& result) noexcept
    {
        constexpr float epsilon = 1.e-5f;
        if (!finite(start) || !finite(middle) || !finite(end)
            || !finite(target) || !finite(pole)) return false;
        const float upper = math::length(middle - start);
        const float lower = math::length(end - middle);
        if (!std::isfinite(upper) || !std::isfinite(lower)
            || upper <= epsilon || lower <= epsilon) return false;

        math::vector3 toTarget = target - start;
        float distance = math::length(toTarget);
        if (!std::isfinite(distance)) return false;
        if (distance <= epsilon)
        {
            toTarget = end - start;
            distance = math::length(toTarget);
            if (distance <= epsilon) toTarget = { 0.f, 0.f, 1.f }, distance = 1.f;
        }
        const math::vector3 axis = toTarget / distance;
        const float reach = std::clamp(distance,
            (std::max)(std::abs(upper - lower) + epsilon, epsilon),
            upper + lower - epsilon);
        math::vector3 bend = pole - start;
        bend = bend - axis * math::dot(bend, axis);
        if (math::length(bend) <= epsilon)
        {
            bend = middle - start;
            bend = bend - axis * math::dot(bend, axis);
        }
        bend = math::length(bend) > epsilon
            ? math::normalize(bend) : perpendicular(axis);
        const float along = (upper * upper - lower * lower + reach * reach)
            / (2.f * reach);
        const float height = std::sqrt((std::max)(0.f, upper * upper - along * along));
        result.middle = start + axis * along + bend * height;
        result.end = start + axis * reach;
        return finite(result.middle) && finite(result.end);
    }

    [[nodiscard]] inline math::quaternion rotation_between(math::vector3 from,
        math::vector3 to) noexcept
    {
        constexpr float epsilon = 1.e-6f;
        const float fromLength = math::length(from);
        const float toLength = math::length(to);
        if (!std::isfinite(fromLength) || !std::isfinite(toLength)
            || fromLength <= epsilon || toLength <= epsilon)
            return math::quaternion::identity();
        from = from / fromLength;
        to = to / toLength;
        const float cosine = std::clamp(math::dot(from, to), -1.f, 1.f);
        if (cosine >= 1.f - epsilon) return math::quaternion::identity();
        math::vector3 axis = math::cross(from, to);
        if (math::length(axis) <= epsilon) axis = perpendicular(from);
        return math::quaternion_from_axis_angle(axis, std::acos(cosine));
    }
}
