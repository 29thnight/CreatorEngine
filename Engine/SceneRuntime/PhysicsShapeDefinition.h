#pragma once

#include "CollisionGeometrySource.h"
#include "ProjectLayerSettings.h"
#include <functional>
#include <string>
#include <mathematics/views.hpp>
#include <algorithm>
#include <cmath>
#include <new>
#include <ranges>
#include <vector>
#include <charconv>

enum class PhysicsShapeKind : std::uint8_t
{
    box,
    sphere,
    capsule,
    convex,
    triangle_mesh,
    heightfield
};

// Serialized authoring values. SDK geometry is constructed as a typed variant.
// Every entry belongs to this body; hierarchy never implicitly transfers ownership.
struct [[reflgen::reflect]] PhysicsShapeDefinition final
{
    std::uint32_t shapeId = 1;
    std::string contactRole; // Canonical role UUID; empty means no contact subscription role.
    [[reflgen::hidden]]
    PhysicsShapeKind kind = PhysicsShapeKind::box;
    math::vector3 halfExtent{.5f, .5f, .5f};
    float radius = .5f;
    float halfHeight = .5f;
    std::string geometryAsset; // Canonical asset UUID at the serialization boundary.
    std::uint64_t geometryRevision = 0;
    math::vector3 geometryScale{1, 1, 1};
    math::vector3 localPosition{};
    math::quaternion localRotation{};
    float staticFriction = .5f;
    float dynamicFriction = .5f;
    float restitution = 0.f;
    bool sensor = false;
    bool queryEnabled = true;
    [[reflgen::hidden]]
    std::uint64_t layerOverride = 0; // Stable project layer ID; zero inherits the Entity layer.
};

namespace ce::physics
{
// Called only with a validated canonical UUID at authoring/body construction boundaries.
inline contact_role_id ContactRoleValue(std::string_view text)
{
    contact_role_id value;
    if (text.empty())
        return value;

    const auto part = [&](std::size_t offset, std::size_t length, auto& target) {
        std::from_chars(text.data() + offset, text.data() + offset + length, target, 16);
    };
    part(0, 8, value.a);
    part(9, 4, value.b);
    part(14, 4, value.c);
    part(19, 2, value.d[0]);
    part(21, 2, value.d[1]);
    for (std::size_t index = 2; index < value.d.size(); ++index)
        part(24 + (index - 2) * 2, 2, value.d[index]);

    return value;
}

// Low-frequency authoring conversion; returned storage owns all shape values.
// Nonuniform scale combined with a rotated primitive can introduce shear. Reject
// it instead of silently approximating a different collision shape.
namespace detail
{
template<bool ResolveCooked, class Resolver>
result<std::vector<ShapeInstance>> BuildPhysicsShapesImpl(std::span<const PhysicsShapeDefinition> definitions,
                                                          math::vector3 scale, collision_filter filter,
                                                          body_kind motion, Resolver&& resolver,
                                                          const project_layer_snapshot* settings)
{
    const auto finiteVector = [](const math::vector3& value) {
        return std::ranges::all_of(math::components(value), [](float part) { return std::isfinite(part); });
    };
    const auto positiveVector = [&](const math::vector3& value) {
        return finiteVector(value) && std::ranges::all_of(math::components(value), [](float part) { return part > 0; });
    };

    if (!positiveVector(scale) || definitions.empty() || definitions.size() > 65535)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid physics shape count or world scale"});

    if (motion != body_kind::static_body && motion != body_kind::kinematic && motion != body_kind::dynamic)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid physics body kind"});

    if (motion != body_kind::static_body && std::ranges::all_of(definitions, &PhysicsShapeDefinition::sensor))
        return std::unexpected(error{error_code::invalid_argument, 0, "Moving body requires a solid shape"});

    const bool uniform = scale.x == scale.y && scale.y == scale.z;

    try
    {
        std::vector<ShapeInstance> shapes;
        std::vector<std::uint32_t> ids;
        shapes.reserve(definitions.size());
        ids.reserve(definitions.size());

        for (const auto& definition : definitions)
        {
            if (!definition.contactRole.empty())
            {
                Uuid::Uuid16 role;
                if (definition.contactRole.size() != 36 || !Uuid::TryParse(definition.contactRole, role) ||
                    role.IsNil() || Uuid::ToString(role) != definition.contactRole)
                    return std::unexpected(error{error_code::invalid_argument, 0, "Invalid contact role UUID"});
            }

            const auto& rotation = definition.localRotation;
            const float norm =
                rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
            if (!definition.shapeId || !finiteVector(definition.localPosition) || !std::isfinite(norm) ||
                std::abs(norm - 1.f) > 1.e-4f || !std::isfinite(definition.staticFriction) ||
                !std::isfinite(definition.dynamicFriction) || !std::isfinite(definition.restitution) ||
                definition.staticFriction < 0 || definition.dynamicFriction < 0 || definition.restitution < 0 ||
                definition.restitution > 1)
                return std::unexpected(error{error_code::invalid_argument, 0, "Invalid shape ID, pose or material"});

            if (!uniform && (rotation.x != 0 || rotation.y != 0 || rotation.z != 0))
                return std::unexpected(
                    error{error_code::unsupported_geometry, 0, "Rotated shape requires uniform body scale"});

            ShapeInstance shape;
            shape.id = shape_id{definition.shapeId};
            shape.contact_role = ContactRoleValue(definition.contactRole);
            shape.local_pose.position = {definition.localPosition.x * scale.x, definition.localPosition.y * scale.y,
                                         definition.localPosition.z * scale.z};
            shape.local_pose.rotation = rotation;
            shape.surface = {definition.staticFriction, definition.dynamicFriction, definition.restitution};
            shape.filter = filter;
            shape.layer_override = ce::layers::layer_id{definition.layerOverride};
            if (definition.layerOverride)
            {
                if (settings)
                {
                    const auto overrideFilter = settings->policy.Filter(settings->catalog, shape.layer_override);
                    if (!overrideFilter)
                        return std::unexpected(
                            error{error_code::invalid_argument, 0, "Unknown or retired shape layer override"});

                    shape.filter = *overrideFilter;
                }
                else if constexpr (ResolveCooked)
                    return std::unexpected(error{error_code::wrong_phase, 0, "Shape layer policy is not bound"});
            }
            shape.sensor = definition.sensor;
            shape.query_enabled = definition.queryEnabled;

            switch (definition.kind)
            {
            case PhysicsShapeKind::box: {
                const math::vector3 extent{definition.halfExtent.x * scale.x, definition.halfExtent.y * scale.y,
                                           definition.halfExtent.z * scale.z};
                if (!positiveVector(definition.halfExtent) || !positiveVector(extent))
                    return std::unexpected(error{error_code::invalid_argument, 0, "Invalid box dimensions"});

                shape.form = box_geometry{extent};
                break;
            }
            case PhysicsShapeKind::sphere: {
                if (!uniform)
                    return std::unexpected(error{error_code::unsupported_geometry, 0, "Sphere requires uniform scale"});

                const float radius = definition.radius * scale.x;
                if (!std::isfinite(radius) || radius <= 0)
                    return std::unexpected(error{error_code::invalid_argument, 0, "Invalid sphere radius"});

                shape.form = sphere_geometry{radius};
                break;
            }
            case PhysicsShapeKind::capsule: {
                // Nonuniform scaling would turn the spherical caps into ellipsoids.
                if (!uniform)
                    return std::unexpected(
                        error{error_code::unsupported_geometry, 0, "Capsule requires uniform scale"});

                const float radius = definition.radius * scale.x;
                const float halfHeight = definition.halfHeight * scale.y;
                if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(definition.halfHeight) ||
                    definition.halfHeight < 0 || !std::isfinite(halfHeight) || halfHeight < 0)
                    return std::unexpected(error{error_code::invalid_argument, 0, "Invalid capsule dimensions"});

                shape.form = capsule_geometry{radius, halfHeight};
                break;
            }
            case PhysicsShapeKind::convex:
            case PhysicsShapeKind::triangle_mesh:
            case PhysicsShapeKind::heightfield: {
                geometry_asset_key key;
                if (definition.geometryAsset.size() != 36 || !Uuid::TryParse(definition.geometryAsset, key.asset) ||
                    key.asset.IsNil() || !definition.geometryRevision || !positiveVector(definition.geometryScale))
                    return std::unexpected(error{error_code::invalid_argument, 0,
                                                 "Cooked shape requires asset UUID/revision and positive scale"});

                key.revision = definition.geometryRevision;
                const auto kind = static_cast<geometry_kind>(std::to_underlying(definition.kind) -
                                                             std::to_underlying(PhysicsShapeKind::convex));
                if (kind != geometry_kind::convex && (motion != body_kind::static_body || definition.sensor))
                    return std::unexpected(
                        error{error_code::unsupported_geometry, 0, "Mesh/heightfield requires static solid body"});

                const math::vector3 cookedScale{definition.geometryScale.x * scale.x,
                                                definition.geometryScale.y * scale.y,
                                                definition.geometryScale.z * scale.z};
                if (!positiveVector(cookedScale))
                    return std::unexpected(error{error_code::invalid_argument, 0, "Cooked shape scale overflow"});

                if constexpr (ResolveCooked)
                {
                    auto asset = std::invoke(resolver, key);
                    if (!asset)
                        return std::unexpected(asset.error());
                    if (!*asset || (*asset)->kind() != kind)
                        return std::unexpected(
                            error{error_code::unsupported_geometry, 0, "Collision geometry asset kind mismatch"});
                    shape.form = cooked_geometry{std::move(*asset), cookedScale};
                }
                else
                    shape.form = cooked_geometry{{}, cookedScale}; // Validation-only value never escapes.
                break;
            }
            default:
                return std::unexpected(error{error_code::unsupported_geometry, 0, "Unknown shape kind"});
            }

            if (!finiteVector(shape.local_pose.position))
                return std::unexpected(error{error_code::invalid_argument, 0, "Scaled shape position overflow"});

            ids.push_back(definition.shapeId);
            shapes.push_back(std::move(shape));
        }

        std::ranges::sort(ids);
        if (std::ranges::adjacent_find(ids) != ids.end())
            return std::unexpected(error{error_code::invalid_argument, 0, "Duplicate shape IDs within body"});

        return shapes;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Shape authoring allocation failed"});
    }
}
} // namespace detail

struct MissingGeometryResolver
{
    result<std::shared_ptr<const CollisionGeometry>> operator()(geometry_asset_key) const
    {
        return std::unexpected(error{error_code::wrong_phase, 0, "Collision geometry resolver is not bound"});
    }
};

inline result<void> ValidatePhysicsShapes(std::span<const PhysicsShapeDefinition> definitions, math::vector3 scale,
                                          collision_filter filter, body_kind motion,
                                          const project_layer_snapshot* settings = nullptr)
{
    return detail::BuildPhysicsShapesImpl<false>(definitions, scale, filter, motion, MissingGeometryResolver{},
                                                 settings)
        .transform([](auto&&) {});
}

template<class Resolver = MissingGeometryResolver>
[[nodiscard]] result<std::vector<ShapeInstance>> BuildPhysicsShapes(std::span<const PhysicsShapeDefinition> definitions,
                                                                    math::vector3 scale, collision_filter filter,
                                                                    body_kind motion, Resolver resolver = {},
                                                                    const project_layer_snapshot* settings = nullptr)
{
    if (!settings && std::ranges::any_of(definitions, [](const auto& shape) { return shape.layerOverride != 0; }))
        return std::unexpected(error{error_code::wrong_phase, 0, "Shape layer policy is not bound"});

    // Complete preflight before the first loader/cook invocation.
    return ValidatePhysicsShapes(definitions, scale, filter, motion, settings).and_then([&] {
        return detail::BuildPhysicsShapesImpl<true>(definitions, scale, filter, motion, resolver, settings);
    });
}
} // namespace ce::physics
