#pragma once

#include "Component.h"
#include "ScenePhysicsSimulation.h"
#include "PhysicsShapeDefinition.h"
#include <optional>

class Scene;

// One complete body definition; shape ownership never follows the Entity hierarchy.
class [[reflgen::reflect]] PhysicsBodyComponent final : public meta::identity<PhysicsBodyComponent, Component>
{
    friend struct reflgen::access;
    friend class Scene;

  public:
    PhysicsBodyComponent() = default;
    PhysicsBodyComponent(ce::physics::body_kind motion, std::vector<PhysicsShapeDefinition> shapes);

    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void OnUninitializing() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnBeforeDeserialize(const Authoring::NodeView& view) const;
    void OnDeserialized(const Authoring::NodeView& view);

    std::span<const PhysicsShapeDefinition> Shapes() const noexcept { return m_shapes; }
    ce::physics::result<void> SetShapes(std::vector<PhysicsShapeDefinition> shapes);

    // Runtime controls; use current world scale, preserve simulated pose and velocities.
    ce::physics::result<void> ReplaceShapes(std::vector<PhysicsShapeDefinition> shapes);
    ce::physics::result<void> RefreshRuntimeShapes();

    ce::physics::result<ce::physics::body_definition> CaptureDefinition() const;
    ce::physics::body_handle RuntimeHandle() const;
    ce::physics::result<ce::physics::body_state> ReadState() const;
    ce::physics::result<void> SetVelocity(math::vector3 linear, math::vector3 angular = {});
    ce::physics::result<void> ApplyForce(math::vector3 linear, math::vector3 angular = {},
                                         ce::physics::force_mode mode = ce::physics::force_mode::force);

  private:
    void ChangeEnabled(bool enabled);
    ce::physics::result<ce::physics::body_definition> CaptureDefinition(
        std::span<const PhysicsShapeDefinition> shapes) const;

    ce::physics::body_kind m_motion = ce::physics::body_kind::dynamic;
    [[reflgen::hidden]]
    std::uint32_t m_shapeSchema = 1;
    [[reflgen::hidden]]
    std::vector<PhysicsShapeDefinition> m_shapes{PhysicsShapeDefinition{}};
    float m_mass = 1.f;
    bool m_gravityEnabled = true;
    ce::physics::axis_lock m_translationLocks = ce::physics::axis_lock::none;
    ce::physics::axis_lock m_rotationLocks = ce::physics::axis_lock::none;
    float m_linearDamping = 0.f;
    float m_angularDamping = .05f;
    math::vector3 m_initialLinearVelocity{};
    math::vector3 m_initialAngularVelocity{};

    [[reflgen::ignore]]
    Scene* m_scene = nullptr;

    [[reflgen::ignore]]
    ScenePhysicsSimulation::binding_id m_binding = 0;

    [[reflgen::ignore]]
    std::optional<ce::physics::body_state> m_transferState;
};
