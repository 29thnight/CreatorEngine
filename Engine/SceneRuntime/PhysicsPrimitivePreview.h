#pragma once

#include "PhysicsShapeDefinition.h"
#include "../Physics/PhysicsCharacter.h"
#include <mathematics/transform.hpp>

namespace ce::physics
{
struct character_preview final
{
    math::matrix4x4 transform;
    float radius, cylinder_height, contact_radius;
    math::vector3 foot, step;
};

// CCT remains upright in world +Y even when the visual entity rotates.
inline result<character_preview> BuildCharacterPreview(const character_desc& capsule)
{
    const float contactRadius = capsule.radius + capsule.contact_offset;
    const float extent = capsule.cylinder_height * .5f + contactRadius;
    if (!valid_character_desc(capsule) || !std::isfinite(contactRadius) || !std::isfinite(extent))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character preview"});

    const auto foot = capsule.position - math::vector3{0, extent, 0};
    const auto step = foot + math::vector3{0, capsule.step_offset, 0};
    if (!std::isfinite(foot.y) || !std::isfinite(step.y))
        return std::unexpected(error{error_code::invalid_argument, 0, "Character preview position overflow"});

    return character_preview{math::compose({1, 1, 1}, {}, capsule.position),
                             capsule.radius,
                             capsule.cylinder_height,
                             contactRadius,
                             foot,
                             step};
}

// Read-only display values. No asset lookup, cooking, registration or SDK access.
struct primitive_preview final
{
    std::variant<box_geometry, sphere_geometry, capsule_geometry> form;
    math::matrix4x4 transform;
    bool sensor;
};

inline result<primitive_preview> BuildPhysicsPrimitivePreview(const PhysicsShapeDefinition& definition,
                                                              math::vector3 scale, pose bodyPose)
{
    if (definition.kind != PhysicsShapeKind::box && definition.kind != PhysicsShapeKind::sphere &&
        definition.kind != PhysicsShapeKind::capsule)
        return std::unexpected(error{error_code::unsupported_geometry, 0, "Preview requires primitive geometry"});

    auto input = definition;
    input.layerOverride = 0; // Display geometry does not select a collision layer.

    auto shapes = detail::BuildPhysicsShapesImpl<false>(std::span{&input, 1}, scale, {}, body_kind::static_body,
                                                        MissingGeometryResolver{}, nullptr);
    if (!shapes)
        return std::unexpected(shapes.error());

    const auto& shape = shapes->front();
    const auto body = math::compose({1, 1, 1}, bodyPose.rotation, bodyPose.position);
    const auto local = math::compose({1, 1, 1}, shape.local_pose.rotation, shape.local_pose.position);
    primitive_preview preview{{box_geometry{}}, local * body, shape.sensor};

    std::visit(
        [&](const auto& form) {
            using T = std::remove_cvref_t<decltype(form)>;
            if constexpr (!std::is_same_v<T, cooked_geometry>)
                preview.form = form;
        },
        shape.form);

    return preview;
}
} // namespace ce::physics
