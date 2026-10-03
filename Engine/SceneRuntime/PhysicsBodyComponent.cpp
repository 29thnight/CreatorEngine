#include "PhysicsTransformPolicy.h"
#include "PhysicsBodyComponent.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"
#include "LogSystem.h"
#include "AuthoringNodeViewAccess.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "DataSystem.h"
#include "CollisionGeometryIO.h"
#include "PathFinder.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Experiment/Cooked/CookedAudioClipSource.h"
#include <stdexcept>

using namespace ce::physics;

PhysicsBodyComponent::PhysicsBodyComponent(body_kind motion, std::vector<PhysicsShapeDefinition> shapes)
    : m_motion(motion), m_shapes(std::move(shapes))
{
    if (auto valid = ValidatePhysicsShapes(m_shapes, {1, 1, 1}, {}, motion); !valid)
        throw std::invalid_argument(std::string(valid.error().message));
}

result<body_definition> PhysicsBodyComponent::CaptureDefinition() const
{
    return CaptureDefinition(m_shapes);
}

result<body_definition> PhysicsBodyComponent::CaptureDefinition(std::span<const PhysicsShapeDefinition> shapes) const
{
    ce::profile_scope scope{ce::marker<"Physics.ShapeAuthoring">()};
    if (!GetOwner())
        return std::unexpected(error{error_code::invalid_argument, 0, "Physics body has no owner"});

    const auto& transform = GetOwner()->Transform_();
    const auto captured = CapturePhysicsTransform(transform.GetWorldMatrix());
    if (!captured)
        return std::unexpected(captured.error());

    const auto scale = captured->scale;

    const auto project = SceneManagers->ProjectLayers();
    if (!project)
        return std::unexpected(error{error_code::wrong_phase, 0, "Project layer settings are not bound"});

    const auto settings = project->Snapshot();
    const auto* layer = settings->catalog.Find(GetOwner()->GetLayer());
    if (!layer)
        return std::unexpected(error{error_code::invalid_argument, 0, "Unknown entity layer"});

    const auto filter = settings->policy.Filter(settings->catalog, layer->id);
    if (!filter)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid entity collision policy"});

    body_definition definition;
    definition.properties.kind = m_motion;
    definition.properties.initial_pose = captured->pose;
    definition.properties.mass = m_mass;
    definition.properties.gravity_enabled = m_gravityEnabled;
    definition.properties.constraints = {m_translationLocks, m_rotationLocks};
    definition.properties.linear_damping = m_linearDamping;
    definition.properties.angular_damping = m_angularDamping;
    definition.properties.linear_velocity = m_initialLinearVelocity;
    definition.properties.angular_velocity = m_initialAngularVelocity;

    if (m_transferState)
    {
        definition.properties.linear_velocity = m_transferState->linear_velocity;
        definition.properties.angular_velocity = m_transferState->angular_velocity;
    }

    auto resolve = [&](geometry_asset_key key) -> result<std::shared_ptr<const CollisionGeometry>> {
        auto* scene = GetOwner()->GetScene();
        if (!scene)
            return std::unexpected(error{error_code::wrong_phase, 0, "Cooked geometry requires scene membership"});

        if (!PathFinder::IsAssetAuthoringEnabled())
            return scene->m_collisionGeometry.ResolveCooked(key, [](geometry_asset_key expected) -> result<std::vector<std::byte>> {
                const auto catalog = DataSystems->GetCookedCatalog();
                if (!catalog)
                    return std::unexpected(error{error_code::invalid_argument, 0, "Player geometry requires CEMF"});

                experiment::cooked::LooseArtifactByteSource bytes(catalog->DerivedRoot());
                std::vector<std::byte> artifact;
                std::string failure;
                if (!catalog->ReadCollisionGeometry({expected.asset}, bytes, artifact, failure))
                    return std::unexpected(error{error_code::invalid_argument, 0, "Player cooked geometry unavailable"});

                return artifact;
            });

        return scene->m_collisionGeometry.Resolve(key, [](geometry_asset_key expected) {
            const auto path = DataSystems->ResolveCatalogAssetPath(FileGuid{expected.asset});
            auto current = CollisionGeometryIO::Read(path, expected);
            if (current || path.empty() || !PathFinder::IsAssetAuthoringEnabled()) return current;

            // Editor history is immutable and addressed by UUID/revision.
            // Player requires packaged closure; it never probes source archives.
            return CollisionGeometryIO::Read(CollisionGeometryIO::RevisionPath(PathFinder::Relative(), expected),
                                              expected);
        });
    };

    return BuildPhysicsShapes(shapes, scale, *filter, m_motion, resolve, settings.get()).transform([&](auto&& built) {
        definition.shapes = std::move(built);
        return std::move(definition);
    });
}

void PhysicsBodyComponent::OnBeforeDeserialize(const Authoring::NodeView& view) const
{
    const auto node = Authoring::NodeViewAccess::Node(view);
    std::uint64_t schema = 0;
    if (!node.IsMap() || node["m_halfExtent"] || !node["m_shapeSchema"].IsScalar() ||
        !Authoring::Scalar::TryParseUInt64(node["m_shapeSchema"].AsString(), schema) || schema != 1 ||
        !node["m_shapes"].IsSequence())
        throw std::runtime_error("Physics body requires shape schema 1; migrate legacy authoring first");
}

void PhysicsBodyComponent::OnDeserialized(const Authoring::NodeView& view)
{
    // ComponentFactory dispatches through the erased descriptor, then invokes
    // this typed hook with the source node. Reject the old schema on that route too.
    OnBeforeDeserialize(view);

    if (m_shapeSchema != 1 || !ValidatePhysicsShapes(m_shapes, {1, 1, 1}, {}, m_motion))
        throw std::runtime_error("Invalid physics body shape definition");
}

result<void> PhysicsBodyComponent::SetShapes(std::vector<PhysicsShapeDefinition> shapes)
{
    if (!m_scene && GetOwner() && GetOwner()->GetScene())
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics body is not registered with its scene"});

    if (m_scene && m_scene->m_physicsSimulation.IsRunning())
        return std::unexpected(error{error_code::wrong_phase, 0, "Shape authoring is frozen during play"});

    if (m_scene)
    {
        auto definition = CaptureDefinition(shapes);
        if (!definition)
            return std::unexpected(definition.error());

        if (auto defined = m_scene->m_physicsSimulation.Define(m_binding, std::move(*definition)); !defined)
            return defined;
    }
    else if (auto built = ValidatePhysicsShapes(shapes, {1, 1, 1}, {}, m_motion); !built)
        return std::unexpected(built.error());

    m_shapes.swap(shapes);
    return {};
}

result<void> PhysicsBodyComponent::ReplaceShapes(std::vector<PhysicsShapeDefinition> shapes)
{
    if (!m_scene || !m_scene->m_physicsSimulation.IsRunning() ||
        m_scene->m_physicsSimulation.Runtime()->status().phase != scene_phase::idle)
        return std::unexpected(error{error_code::wrong_phase, 0, "Shape replacement requires an idle play session"});

    ce::profile_scope scope{ce::marker<"Physics.ShapeRuntimeReplace">()};
    if (!GetOwner() || !m_scene->EnsureResolved(m_scene->HandleOf(GetOwner()->m_index)))
        return std::unexpected(error{error_code::stale_handle, 0, "Shape replacement Transform is unresolved"});

    auto definition = CaptureDefinition(shapes);
    if (!definition)
        return std::unexpected(definition.error());

    if (auto replaced = m_scene->m_physicsSimulation.Replace(m_binding, std::move(*definition)); !replaced)
        return replaced;

    m_shapes.swap(shapes);
    return {};
}

result<void> PhysicsBodyComponent::RefreshRuntimeShapes()
{
    try
    {
        return ReplaceShapes(m_shapes);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Shape refresh allocation failed"});
    }
}

void PhysicsBodyComponent::OnAddedToScene()
{
    if (!GetOwner() || m_scene)
        return;

    auto* scene = GetOwner()->GetScene();
    if (!scene)
        return;

    if (auto added = scene->RegisterPhysicsBody(*this); !added)
        Debug::PrintLog(spdlog::level::err, std::string(added.error().message));
}

void PhysicsBodyComponent::OnRemovingFromScene()
{
    if (!m_scene)
        return;

    if (auto removed = m_scene->UnregisterPhysicsBody(*this); !removed)
        Debug::PrintLog(spdlog::level::err, std::string(removed.error().message));
}

void PhysicsBodyComponent::OnUninitializing()
{
    OnRemovingFromScene();
}

void PhysicsBodyComponent::ChangeEnabled(bool enabled)
{
    if (!m_scene)
        return;

    if (auto changed = m_scene->m_physicsSimulation.SetEnabled(m_binding, enabled); !changed)
        Debug::PrintLog(spdlog::level::err, std::string(changed.error().message));
}

void PhysicsBodyComponent::OnEnable()
{
    ChangeEnabled(true);
}

void PhysicsBodyComponent::OnDisable()
{
    ChangeEnabled(false);
}

body_handle PhysicsBodyComponent::RuntimeHandle() const
{
    return m_scene ? m_scene->m_physicsSimulation.Handle(m_binding) : body_handle{};
}

result<body_state> PhysicsBodyComponent::ReadState() const
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics body is not attached to a scene"});

    return m_scene->m_physicsSimulation.Read(m_binding);
}

result<void> PhysicsBodyComponent::SetVelocity(math::vector3 linear, math::vector3 angular)
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics body is not attached to a scene"});

    return m_scene->m_physicsSimulation.SetVelocity(m_binding, linear, angular);
}

result<void> PhysicsBodyComponent::ApplyForce(math::vector3 linear, math::vector3 angular, force_mode mode)
{
    if (!m_scene)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics body is not attached to a scene"});

    return m_scene->m_physicsSimulation.ApplyForce(m_binding, linear, angular, mode);
}
