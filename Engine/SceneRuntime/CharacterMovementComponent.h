#pragma once
#include "Component.h"
#include "ScenePhysicsSimulation.h"
#include <optional>

class Scene;

// Capsule controller owns no rigid-body component. Inputs are world-space velocity in metres/second.
class [[reflgen::reflect]] CharacterMovementComponent final
    : public meta::identity<CharacterMovementComponent, Component>
{
    friend struct reflgen::access;
    friend class Scene;

  public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void OnUninitializing() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnBeforeDeserialize(const Authoring::NodeView& view) const;
    void OnDeserialized(const Authoring::NodeView& view);

    ce::physics::result<ce::physics::character_desc> CaptureCapsule() const;
    ce::physics::result<ScenePhysicsSimulation::character_definition> CaptureDefinition() const;
    ce::physics::result<ScenePhysicsSimulation::character_motion_state> ReadState() const;
    ce::physics::result<void> SetDesiredVelocity(math::vector3 velocity);
    ce::physics::result<void> Jump();
    ce::physics::result<void> ForceVelocity(math::vector3 velocity, double seconds);
    ce::physics::result<void> CancelForcedVelocity();
    ce::physics::result<void> Teleport(math::vector3 position);
    ce::physics::character_handle RuntimeHandle() const;

  private:
    void ChangeEnabled(bool enabled);

    [[reflgen::hidden]]
    std::uint32_t m_characterSchema = 1;
    float m_radius = .5f;
    float m_cylinderHeight = 1.f;
    float m_contactOffset = .05f;
    float m_stepOffset = .3f;
    float m_slopeLimitCosine = .70710678f;
    float m_gravity = -9.81f;
    float m_minimumDistance = .001f;
    float m_acceleration = 20.f;
    float m_brakingDecay = 8.f;
    float m_jumpSpeed = 5.f;
    float m_maxFallSpeed = 55.f;
    math::vector3 m_initialVelocity{};

    [[reflgen::ignore]]
    Scene* m_scene = nullptr;
    [[reflgen::ignore]]
    ScenePhysicsSimulation::binding_id m_binding = 0;
    [[reflgen::ignore]]
    std::optional<ScenePhysicsSimulation::character_motion_state> m_transferState;
};
