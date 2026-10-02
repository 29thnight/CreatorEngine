#pragma once

#include "../Physics/PhysicsGeometry.h"
#include "../Utility_Framework/Uuid.h"
#include <mathematics/views.hpp>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <vector>

namespace ce::physics
{
struct geometry_asset_key
{
    Uuid::Uuid16 asset;
    std::uint64_t revision = 0;
    auto operator<=>(const geometry_asset_key&) const = default;
};

struct convex_source
{
    std::vector<math::vector3> points;
};

struct triangle_mesh_source
{
    std::vector<math::vector3> points;
    std::vector<triangle_indices> triangles;
};

struct heightfield_source
{
    std::uint32_t rows = 0, columns = 0;
    std::vector<std::int16_t> heights;
};

using collision_geometry_source = std::variant<convex_source, triangle_mesh_source, heightfield_source>;

struct CollisionGeometrySource
{
    geometry_asset_key key;
    collision_geometry_source form;
};

inline result<void> ValidateCollisionGeometrySource(const CollisionGeometrySource& source)
{
    if (source.key.asset.IsNil() || !source.key.revision || source.form.valueless_by_exception())
        return std::unexpected(error{error_code::invalid_argument, 0, "Collision geometry requires UUID and revision"});

    const auto finite = [](const math::vector3& point) {
        return std::ranges::all_of(math::components(point), [](float value) { return std::isfinite(value); });
    };

    return std::visit(
        [&](const auto& input) -> result<void> {
            using T = std::remove_cvref_t<decltype(input)>;
            if constexpr (std::same_as<T, heightfield_source>)
            {
                const auto count = std::uint64_t(input.rows) * input.columns;
                if (input.rows < 2 || input.columns < 2 || count > 16 * 1024 * 1024 || count != input.heights.size())
                    return std::unexpected(error{error_code::invalid_argument, 0, "Invalid heightfield source"});
            }
            else
            {
                constexpr auto minimum = std::same_as<T, convex_source> ? 4u : 3u;
                if (input.points.size() < minimum || input.points.size() > 1024 * 1024 ||
                    !std::ranges::all_of(input.points, finite))
                    return std::unexpected(
                        error{error_code::invalid_argument, 0, "Invalid collision geometry vertices"});

                if constexpr (std::same_as<T, triangle_mesh_source>)
                {
                    if (input.triangles.empty() || input.triangles.size() > 4 * 1024 * 1024 ||
                        !std::ranges::all_of(input.triangles, [&](const auto& triangle) {
                            return triangle.a < input.points.size() && triangle.b < input.points.size() &&
                                   triangle.c < input.points.size() && triangle.a != triangle.b &&
                                   triangle.b != triangle.c && triangle.a != triangle.c;
                        }))
                        return std::unexpected(
                            error{error_code::invalid_argument, 0, "Invalid collision geometry indices"});
                }
            }

            return {};
        },
        source.form);
}
} // namespace ce::physics
