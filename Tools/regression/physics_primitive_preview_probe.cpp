#include "../../Engine/SceneRuntime/PhysicsPrimitivePreview.h"
#include "../../Engine/RenderEngine/EnhancedGizmoSceneTypes.h"
#include <iostream>
#include <stdexcept>

int main()
{
    int checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value)
            throw std::runtime_error("Primitive preview check " + std::to_string(checks));
    };

    try
    {
        PhysicsShapeDefinition shape;
        shape.halfExtent = {1, 2, 3};
        shape.localPosition = {1, 2, 3};
        shape.sensor = true;
        shape.layerOverride = UINT64_MAX;
        auto box = ce::physics::BuildPhysicsPrimitivePreview(shape, {2, 3, 4}, {{10, 20, 30}, {}});
        check(bool(box));
        check(box && box->sensor);
        check(box && std::get<ce::physics::box_geometry>(box->form).half_extent == math::vector3{2, 6, 12});
        check(box && box->transform.translation() == math::vector3{12, 26, 42});
        check(box && math::length(box->transform.up()) == 1); // Dimensions baked exactly once.

        EnhancedGizmoLineCollector lines;
        const auto color = math::color{1, 1, 0, 1};
        lines.AddWireBox(box->transform, std::get<ce::physics::box_geometry>(box->form).half_extent, color);
        check(lines.GetVertices().size() == 24);
        math::vector3 low{1000, 1000, 1000}, high{-1000, -1000, -1000};

        for (const auto& vertex : lines.GetVertices())
        {
            low = {(std::min)(low.x, vertex.position.x), (std::min)(low.y, vertex.position.y),
                   (std::min)(low.z, vertex.position.z)};
            high = {(std::max)(high.x, vertex.position.x), (std::max)(high.y, vertex.position.y),
                    (std::max)(high.z, vertex.position.z)};
        }

        check(low == math::vector3{10, 20, 30});
        check(high == math::vector3{14, 32, 54});

        shape.kind = PhysicsShapeKind::sphere;
        shape.radius = 2;
        auto sphere = ce::physics::BuildPhysicsPrimitivePreview(shape, {3, 3, 3}, {});
        check(sphere && std::get<ce::physics::sphere_geometry>(sphere->form).radius == 6);
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 2, 1}, {}));

        shape.kind = PhysicsShapeKind::capsule;
        shape.halfHeight = 4;
        auto capsule = ce::physics::BuildPhysicsPrimitivePreview(shape, {2, 2, 2}, {});
        check(capsule && std::get<ce::physics::capsule_geometry>(capsule->form).half_height == 8);
        check(capsule && std::get<ce::physics::capsule_geometry>(capsule->form).radius == 4);
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 2, 1}, {}));
        shape.halfHeight = 0;
        check(bool(ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 1, 1}, {})));

        shape.localRotation = {0, 0, .70710678f, .70710678f};
        auto rotated = ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 1, 1}, {});
        check(rotated && std::abs(rotated->transform.up().x) > .999f); // Capsule axis is local Y.
        lines.Reset();
        lines.AddWireCapsule(rotated->transform, std::get<ce::physics::capsule_geometry>(rotated->form).radius, 8,
                             color);
        float minX = 1000, maxX = -1000;

        for (const auto& vertex : lines.GetVertices())
        {
            minX = (std::min)(minX, vertex.position.x);
            maxX = (std::max)(maxX, vertex.position.x);
        }

        check(std::abs(minX + 5) < .001f && std::abs(maxX - 7) < .001f);
        shape.kind = PhysicsShapeKind::box;
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 2, 1}, {}));
        shape.localRotation = {};
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {-1, 1, 1}, {}));
        shape.kind = PhysicsShapeKind::triangle_mesh;
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 1, 1}, {}));
        shape.kind = PhysicsShapeKind::heightfield;
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 1, 1}, {}));
        shape.kind = PhysicsShapeKind::convex;
        check(!ce::physics::BuildPhysicsPrimitivePreview(shape, {1, 1, 1}, {}));
        ce::physics::character_desc character;
        character.position = {2, 5, 3};
        auto preview = ce::physics::BuildCharacterPreview(character);
        check(bool(preview));
        check(preview && preview->transform.translation() == character.position);
        check(preview && preview->transform.up() == math::vector3{0, 1, 0});
        check(preview && std::abs(preview->foot.y - 3.95f) < .0001f);
        check(preview && std::abs(preview->step.y - 4.25f) < .0001f);
        check(preview && preview->contact_radius == .55f);
        character.radius = -1;
        check(!ce::physics::BuildCharacterPreview(character));
        character.radius = (std::numeric_limits<float>::max)();
        character.contact_offset = character.radius;
        check(!ce::physics::BuildCharacterPreview(character));

        std::cout << "PHYSICS_PRIMITIVE_PREVIEW_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
