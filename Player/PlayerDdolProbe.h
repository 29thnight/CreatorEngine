#pragma once
#include "CharacterMovementComponent.h"
#include "PhysicsBodyComponent.h"
#include "Scene.h"
#include "Transform.h"
#include <array>
#include <cstdio>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace Player
{
// Packaged-host regression only. Entity pointers survive DDOL; Scene handles do not.
class DdolProbe
{
    struct Node
    {
        Entity* entity;
        Entity* parent;
        EntityHandle oldHandle;
        ce::layers::layer_id layer;
        bool enabled;
        std::array<float, 10> pose;
        std::vector<Entity*> children;
        CharacterMovementComponent* character;
        PhysicsBodyComponent* body;
        std::optional<ScenePhysicsSimulation::character_motion_state> state;
    };

    static std::array<float, 10> Pose(Entity& entity)
    {
        const auto position = entity.Transform_().GetWorldPosition();
        const auto rotation = entity.Transform_().GetWorldQuaternion();
        const auto scale = entity.Transform_().GetWorldScale();
        return {position.x, position.y, position.z, rotation.x, rotation.y,
                rotation.z, rotation.w, scale.x,    scale.y,    scale.z};
    }

    void Check(bool valid, const char* name)
    {
        if (!valid)
            throw std::runtime_error(name);
        ++m_passed;
    }

  public:
    DdolProbe(Entity& active, bool hierarchy, bool geometry = false)
        : m_active(&active), m_hierarchy(hierarchy), m_geometry(geometry)
    {
        std::vector<Entity*> entities{&active};
        if (geometry)
        {
            if (hierarchy)
                throw std::runtime_error("Geometry and disabled hierarchy fixtures are independent");

            for (const auto name : {"CharacterGateFloor", "GeometryPersistentParent"})
            {
                auto* entity = active.OwnerSceneFind(name);
                if (!entity)
                    throw std::runtime_error("Missing geometry DDOL fixture node");

                entities.push_back(entity);
            }

            const auto stats = active.GetScene()->ReadCollisionGeometryStatistics();
            if (!stats || stats->imports != 3 || stats->cooks != 0)
                throw std::runtime_error("Source geometry must import three cooked assets without runtime cooking");
        }
        if (hierarchy)
        {
            for (const auto name :
                 {"PersistentParent", "PersistentBranch", "PersistentDisabledCharacter", "PersistentDisabledBody"})
            {
                auto* entity = active.OwnerSceneFind(name);
                if (!entity)
                    throw std::runtime_error("Missing hierarchy fixture node");
                entities.push_back(entity);
            }
        }

        for (auto* entity : entities)
        {
            auto* scene = entity->GetScene();
            const auto handle = scene->HandleOf(entity->m_index);
            if (!handle.IsValid() || scene->Resolve(handle) != entity)
                throw std::runtime_error("Source Scene handle did not resolve");

            auto* character = entity->GetComponent<CharacterMovementComponent>();
            auto* body = entity->GetComponent<PhysicsBodyComponent>();
            Node node{entity,
                      entity->GetParentIndex() == 0 ? nullptr : scene->TryGetEntity(entity->GetParentIndex()),
                      handle,
                      entity->GetLayer(),
                      entity->IsEnabled(),
                      Pose(*entity),
                      {},
                      character,
                      body,
                      {}};
            for (const auto index : entity->GetChildrenIndices())
                node.children.push_back(scene->TryGetEntity(index));
            if (character)
            {
                const auto state = character->ReadState();
                if (!state)
                    throw std::runtime_error("Source character state unavailable");
                node.state = *state;
            }
            m_nodes.push_back(std::move(node));
        }
        if (hierarchy && (!m_nodes[3].character || !m_nodes[4].body || m_nodes[3].enabled || m_nodes[4].enabled))
            throw std::runtime_error("Hierarchy fixture requires disabled character and body");
    }

    bool Poll(Scene& destination, std::uint64_t tick)
    {
        if (m_done)
            return true;

        if (m_geometry && m_phase == 3)
        {
            if (tick < m_phaseTick + 60)
                return false;

            const auto state = m_nodes[0].character->ReadState();
            Check(state && state->collision.below && std::abs(state->collision.foot_position.y) < .08f,
                  "Destination mesh landing");
            Check(state && state->collision.position.x > 3 && state->motion.forced_remaining > 0,
                  "Movement continued on destination mesh");
            return Finish("geometry", tick);
        }

        if (m_phase == 0)
        {
            for (const auto& node : m_nodes)
            {
                auto& entity = *node.entity;
                Check(entity.GetScene() == &destination, "Destination owner");
                Check(node.oldHandle.sceneId != destination.GetSceneId() && !destination.Resolve(node.oldHandle),
                      "Retired Scene handle rejection");
                const auto fresh = destination.HandleOf(entity.m_index);
                Check(fresh.IsValid() && fresh != node.oldHandle && destination.Resolve(fresh) == &entity,
                      "New Scene handle identity");
                Check(entity.GetComponent<CharacterMovementComponent>() == node.character &&
                          entity.GetComponent<PhysicsBodyComponent>() == node.body,
                      "Retained component identity");
                Check(entity.IsEnabled() == node.enabled && entity.GetLayer() == node.layer, "Activation and layer");
                const auto pose = Pose(entity);
                Check(std::ranges::all_of(std::views::iota(&entity == m_active ? 3 : 0, 10),
                                          [&](int index) { return std::abs(pose[index] - node.pose[index]) < .0001f; }),
                      "World pose");
                auto* parent = destination.TryGetEntity(entity.GetParentIndex());
                Check(parent == (node.parent ? node.parent : destination.TryGetEntity(0)), "Parent remap");
                Check(entity.GetChildrenIndices().size() == node.children.size() &&
                          std::ranges::equal(entity.GetChildrenIndices(), node.children, {},
                                             [&](Entity::Index index) { return destination.TryGetEntity(index); }),
                      "Child remap");
            }

            if (m_geometry)
            {
                Check(m_nodes[1].body && m_nodes[1].body->RuntimeHandle(), "Destination mesh body recreated");
                const auto stats = destination.ReadCollisionGeometryStatistics();
                Check(stats && stats->imports == 1 && stats->assets == 1, "Destination mesh artifact imported");
                Check(stats && stats->cooks == 0, "Destination did not cook authoring geometry");
                m_phase = 3;
                m_phaseTick = tick;
                return false;
            }

            if (!m_hierarchy)
                return Finish("handles", tick);

            auto& child = m_nodes[3];
            m_disabled = child.character;
            m_body = m_nodes[4].body;
            const auto state = m_disabled->ReadState();
            Check(state && state->desired_velocity == child.state->desired_velocity, "Disabled input retained");
            Check(state && state->tick == child.state->tick &&
                      std::abs(state->collision.position.x - child.state->collision.position.x) < .0001f &&
                      std::abs(state->collision.position.y - child.state->collision.position.y) < .0001f &&
                      std::abs(state->collision.position.z - child.state->collision.position.z) < .0001f,
                  "Disabled state held");
            Check(!m_disabled->RuntimeHandle() && !m_body->RuntimeHandle(), "Disabled SDK handles absent");

            child.entity->SetEnabled(true);
            Check(static_cast<bool>(m_disabled->RuntimeHandle()), "Child controller recreated");
            const auto enabled = m_disabled->ReadState();
            Check(enabled && enabled->desired_velocity.x == 2, "Enabled child input retained");
            Check(m_disabled->ForceVelocity({3, 0, 0}, 10).has_value(), "Reactivated force request");
            m_nodes[4].entity->SetEnabled(true);
            Check(static_cast<bool>(m_body->RuntimeHandle()), "Child body recreated");
            m_nodes[4].entity->SetEnabled(false);
            Check(!m_body->RuntimeHandle(), "Child body retired");
            m_phase = 1;
            m_phaseTick = tick;
            return false;
        }

        if (tick < m_phaseTick + 30)
            return false;

        if (m_phase == 1)
        {
            const auto state = m_disabled->ReadState();
            Check(state && state->tick.value > 0 && state->collision.position.x > m_nodes[3].pose[0] + .5f,
                  "Reactivated child movement");
            Check(state && state->collision.position.y < m_nodes[3].pose[1], "Reactivated child gravity");
            Check(state && state->motion.forced_remaining > 9 && state->motion.forced_remaining < 10,
                  "Reactivated force timer progressed");
            m_nodes[3].entity->SetEnabled(false);
            Check(!m_disabled->RuntimeHandle(), "Child controller retired again");
            m_held = Pose(*m_nodes[3].entity);
            const auto held = m_disabled->ReadState();
            Check(held && held->motion.forced_remaining > 0, "Disabled state readable with remaining force");
            m_heldState = *held;
            m_phase = 2;
            m_phaseTick = tick;
            return false;
        }

        Check(std::ranges::equal(Pose(*m_nodes[3].entity), m_held,
                                 [](float a, float b) { return std::abs(a - b) < .0001f; }),
              "Disabled world pose stopped");
        const auto state = m_disabled->ReadState();
        Check(state && state->tick == m_heldState.tick &&
                  state->motion.forced_remaining == m_heldState.motion.forced_remaining,
              "Disabled tick and timer stopped");
        Check(std::ranges::equal(Pose(*m_nodes[4].entity), m_nodes[4].pose,
                                 [](float a, float b) { return std::abs(a - b) < .0001f; }) &&
                  !m_body->RuntimeHandle(),
              "Disabled body held");
        return Finish("hierarchy", tick);
    }

  private:
    bool Finish(const char* kind, std::uint64_t tick)
    {
        m_done = true;
        std::printf("[physics.player.%s] {\"passed\":%u,\"failed\":0,\"nodes\":%zu,\"tick\":%llu,\"complete\":true}\n",
                    kind, m_passed, m_nodes.size(), static_cast<unsigned long long>(tick));
        std::fflush(stdout);
        return true;
    }

    Entity* m_active;
    bool m_hierarchy, m_geometry, m_done = false;
    unsigned m_passed = 0, m_phase = 0;
    std::uint64_t m_phaseTick = 0;
    std::vector<Node> m_nodes;
    CharacterMovementComponent* m_disabled = nullptr;
    PhysicsBodyComponent* m_body = nullptr;
    std::array<float, 10> m_held{};
    ScenePhysicsSimulation::character_motion_state m_heldState{};
};
} // namespace Player
