#include "CharacterMovementComponent.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"
#include "LogSystem.h"
#include "AuthoringNodeViewAccess.h"
#include <cmath>
#include <stdexcept>

using namespace ce::physics;

result<character_desc> CharacterMovementComponent::CaptureCapsule() const
{
    if (!GetOwner())
        return std::unexpected(error{error_code::invalid_argument, 0, "Character has no owner"});
    const auto& transform = GetOwner()->Transform_();
    const auto scale = transform.GetWorldScale();
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) || scale.x <= 0 ||
        std::abs(scale.x - scale.y) > scale.x * 1e-5f || std::abs(scale.x - scale.z) > scale.x * 1e-5f)
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Character capsule requires positive uniform world scale"});

    character_desc capsule;
    capsule.position = transform.GetWorldPosition();
    capsule.radius = m_radius * scale.x;
    capsule.cylinder_height = m_cylinderHeight * scale.x;
    capsule.contact_offset = m_contactOffset * scale.x;
    capsule.step_offset = m_stepOffset * scale.x;
    capsule.slope_limit_cosine = m_slopeLimitCosine;
    if (!valid_character_desc(capsule))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character capsule"});

    return capsule;
}

result<ScenePhysicsSimulation::character_definition> CharacterMovementComponent::CaptureDefinition() const
{
    auto capsule = CaptureCapsule();
    if (!capsule)
        return std::unexpected(capsule.error());

    const auto project = SceneManagers->ProjectLayers();
    if (!project)
        return std::unexpected(error{error_code::wrong_phase, 0, "Project layer settings are not bound"});
    const auto settings = project->Snapshot();
    auto filter = settings->policy.Filter(settings->catalog, GetOwner()->GetLayer());
    if (!filter)
        return std::unexpected(error{error_code::invalid_argument, 0, "Unknown character layer"});

    ScenePhysicsSimulation::character_definition value;
    value.capsule = *capsule;
    value.capsule.belongs_to = filter->belongs_to;
    value.capsule.collides_with = filter->collides_with;
    value.gravity = m_gravity;
    value.minimum_distance = m_minimumDistance;
    value.motion = {m_acceleration, m_brakingDecay, m_jumpSpeed, m_maxFallSpeed};
    value.initial_motion = m_transferState ? m_transferState->motion : character_motion_memory{};
    value.initial_velocity = m_transferState ? m_transferState->desired_velocity : m_initialVelocity;
    value.initial_fall_velocity = m_transferState ? m_transferState->fall_velocity : 0;
    if (!valid_motion_parameters(value.motion) || !valid_motion_memory(value.initial_motion) ||
        !valid_character_desc(value.capsule) || !std::isfinite(value.gravity) ||
        !std::isfinite(value.minimum_distance) || value.minimum_distance < 0 ||
        !std::isfinite(value.initial_velocity.x) || !std::isfinite(value.initial_velocity.y) ||
        !std::isfinite(value.initial_velocity.z))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character definition"});
    return value;
}

void CharacterMovementComponent::OnBeforeDeserialize(const Authoring::NodeView& view) const
{
    const auto node = Authoring::NodeViewAccess::Node(view);
    std::uint64_t schema = 0;
    if (!node.IsMap() || !node["m_characterSchema"].IsScalar() ||
        !Authoring::Scalar::TryParseUInt64(node["m_characterSchema"].AsString(), schema) || schema != 1)
        throw std::runtime_error("Character requires schema 1; migrate legacy authoring first");
}

void CharacterMovementComponent::OnDeserialized(const Authoring::NodeView& view)
{
    OnBeforeDeserialize(view);
    character_desc capsule;
    capsule.radius = m_radius;
    capsule.cylinder_height = m_cylinderHeight;
    capsule.contact_offset = m_contactOffset;
    capsule.step_offset = m_stepOffset;
    capsule.slope_limit_cosine = m_slopeLimitCosine;
    if (!valid_motion_parameters({m_acceleration, m_brakingDecay, m_jumpSpeed, m_maxFallSpeed}) ||
        !valid_character_desc(capsule) || !std::isfinite(m_gravity) || !std::isfinite(m_minimumDistance) ||
        m_minimumDistance < 0 || !std::isfinite(m_initialVelocity.x) || !std::isfinite(m_initialVelocity.y) ||
        !std::isfinite(m_initialVelocity.z))
        throw std::runtime_error("Invalid authored character movement definition");
}

void CharacterMovementComponent::OnAddedToScene()
{
    if (!GetOwner() || m_scene || !GetOwner()->GetScene())
        return;
    if (auto added = GetOwner()->GetScene()->RegisterCharacterMovement(*this); !added)
        Debug::PrintLog(spdlog::level::err, std::string(added.error().message));
}

void CharacterMovementComponent::OnRemovingFromScene()
{
    if (!m_scene)
        return;
    if (auto removed = m_scene->UnregisterCharacterMovement(*this); !removed)
        Debug::PrintLog(spdlog::level::err, std::string(removed.error().message));
}

void CharacterMovementComponent::OnUninitializing()
{
    OnRemovingFromScene();
}

void CharacterMovementComponent::ChangeEnabled(bool enabled)
{
    if (!m_scene)
        return;
    if (auto changed = m_scene->m_physicsSimulation.SetCharacterEnabled(m_binding, enabled); !changed)
        Debug::PrintLog(spdlog::level::err, std::string(changed.error().message));
}

void CharacterMovementComponent::OnEnable()
{
    ChangeEnabled(true);
}
void CharacterMovementComponent::OnDisable()
{
    ChangeEnabled(false);
}

result<ScenePhysicsSimulation::character_motion_state> CharacterMovementComponent::ReadState() const
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character is not attached"});
    return m_scene->m_physicsSimulation.ReadCharacter(m_binding);
}

result<void> CharacterMovementComponent::SetDesiredVelocity(math::vector3 velocity)
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character is not attached"});
    return m_scene->m_physicsSimulation.SetCharacterVelocity(m_binding, velocity);
}

result<void> CharacterMovementComponent::Jump()
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character is not attached"});
    return m_scene->m_physicsSimulation.JumpCharacter(m_binding);
}

result<void> CharacterMovementComponent::ForceVelocity(math::vector3 velocity, double seconds)
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character is not attached"});
    return m_scene->m_physicsSimulation.ForceCharacter(m_binding, velocity, seconds);
}

result<void> CharacterMovementComponent::CancelForcedVelocity()
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character is not attached"});
    return m_scene->m_physicsSimulation.CancelCharacterForce(m_binding);
}

result<void> CharacterMovementComponent::Teleport(math::vector3 position)
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character is not attached"});
    auto moved = m_scene->m_physicsSimulation.TeleportCharacter(m_binding, position);
    if (moved)
        GetOwner()->Transform_().SetWorldPosition(position);
    return moved;
}

character_handle CharacterMovementComponent::RuntimeHandle() const
{
    return m_scene ? m_scene->m_physicsSimulation.CharacterHandle(m_binding) : character_handle{};
}
