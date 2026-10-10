#pragma once
#include "../Physics/PhysicsScene.h"
#include "ProjectLayerSettings.h"
#include "CharacterMotionPolicy.h"
#include <unordered_map>
#include <thread>
#include <memory>
#include <span>
#include <vector>
#include <algorithm>
#include <functional>

// Scene-owned runtime. Registration is authoring membership; no SDK scene exists in editor mode.
class ScenePhysicsSimulation final
{
  public:
    using binding_id = std::uint64_t;

    struct character_definition
    {
        ce::physics::character_desc capsule;
        math::vector3 initial_velocity{};
        float gravity = -9.81f;
        float initial_fall_velocity = 0;
        float minimum_distance = .001f;
        ce::physics::character_motion_parameters motion;
        ce::physics::character_motion_memory initial_motion;
    };

    struct character_motion_state
    {
        ce::physics::character_state collision{};
        math::vector3 desired_velocity{};
        float fall_velocity = 0;
        ce::physics::tick_id tick{};
        ce::physics::character_motion_memory motion;
    };

    struct changed_character
    {
        binding_id binding;
        character_motion_state state;
    };

    struct contact_event
    {
        binding_id first, second;
        ce::physics::tick_id tick;
        ce::physics::collision_event value;
    };

    std::span<const contact_event> Contacts() const noexcept { return m_contacts; }

    struct changed_pose
    {
        binding_id binding;
        ce::physics::pose value;
    };

    struct render_pose
    {
        binding_id binding;
        ce::physics::pose previous, current;
    };

    struct layer_assignment
    {
        binding_id binding;
        ce::layers::layer_id layer;
    };

    // Complete, binding-sorted membership; publish only after all SDK filters succeed.
    ce::physics::result<void> CommitLayers(const project_layer_snapshot& settings,
                                           std::span<const layer_assignment> assignments);
    std::uint64_t LayerRevision() const noexcept { return m_layerRevision; }

    ScenePhysicsSimulation() = default;
    ~ScenePhysicsSimulation();
    ScenePhysicsSimulation(const ScenePhysicsSimulation&) = delete;
    ScenePhysicsSimulation& operator=(const ScenePhysicsSimulation&) = delete;

    ce::physics::result<binding_id> Register(ce::physics::body_definition definition, bool enabled);

    ce::physics::result<void> Unregister(binding_id binding);
    ce::physics::result<binding_id> RegisterCharacter(character_definition definition, bool enabled);
    ce::physics::result<void> UnregisterCharacter(binding_id binding);
    ce::physics::result<void> DefineCharacter(binding_id binding, character_definition definition);
    ce::physics::result<void> SetCharacterEnabled(binding_id binding, bool enabled);
    ce::physics::result<void> SetCharacterVelocity(binding_id binding, math::vector3 velocity);
    ce::physics::result<void> JumpCharacter(binding_id binding);
    ce::physics::result<void> ForceCharacter(binding_id binding, math::vector3 velocity, double seconds);
    ce::physics::result<void> CancelCharacterForce(binding_id binding);
    ce::physics::result<void> TeleportCharacter(binding_id binding, math::vector3 position);
    ce::physics::result<character_motion_state> ReadCharacter(binding_id binding) const;
    ce::physics::character_handle CharacterHandle(binding_id binding) const;
    ce::physics::result<void> Define(binding_id binding, ce::physics::body_definition definition);
    ce::physics::result<void> SetEnabled(binding_id binding, bool enabled);

    // Explicit runtime replacement, owner/idle only. Preserve pose/velocities and enabled membership.
    ce::physics::result<void> Replace(binding_id binding, ce::physics::body_definition definition);

    ce::physics::result<void> Start(const ce::physics::scene_config& config = {});
    ce::physics::result<void> Stop();

    using BeforeStep = std::function<ce::physics::result<void>(double fixedSeconds, double droppedSeconds)>;
    ce::physics::result<std::uint32_t> Advance(double frame_seconds, const BeforeStep& beforeStep = {});

    ce::physics::result<ce::physics::body_state> Read(binding_id binding) const;
    ce::physics::body_handle Handle(binding_id binding) const;
    ce::physics::result<binding_id> Binding(ce::physics::body_handle body) const;

    // Owner-thread, idle SDK control. Editor/disabled bodies reject runtime mutations.
    ce::physics::result<void> SetVelocity(binding_id binding, math::vector3 linear, math::vector3 angular);
    ce::physics::result<void> ApplyForce(binding_id binding, math::vector3 linear, math::vector3 angular,
                                         ce::physics::force_mode mode = ce::physics::force_mode::force);

    ce::physics::result<void> SetPose(binding_id binding, const ce::physics::pose& value);
    ce::physics::result<void> SetKinematicTarget(binding_id binding, const ce::physics::pose& value);
    ce::physics::result<ce::physics::pose> RenderPose(binding_id binding) const;
    std::span<const render_pose> RenderPoses() const noexcept { return m_render; }
    double InterpolationAlpha() const noexcept { return std::clamp(m_accumulator / fixed_seconds, 0.0, 1.0); }

    ce::physics::result<void> QueryBatch(std::span<const ce::physics::query_request> requests,
                                        std::span<ce::physics::result<ce::physics::query_result>> results);

    bool IsRunning() const noexcept { return bool(m_runtime); }
    std::span<const changed_pose> ChangedPoses() const noexcept { return m_changed; }
    std::span<const changed_character> ChangedCharacters() const noexcept { return m_changedCharacters; }
    ce::physics::PhysicsScene* Runtime() const noexcept { return m_runtime.get(); }
    double DroppedSeconds() const noexcept { return m_dropped; }

    static constexpr double fixed_seconds = 1.0 / 60.0;
    static constexpr std::uint32_t max_catchup_ticks = 4;

  private:
#if defined(CE_PHYSICS_TESTING)
    friend struct ScenePhysicsRetirementProbe;
#endif

    struct character_entry
    {
        character_definition definition;
        character_motion_state state{};
        ce::physics::character_handle handle{};
        bool enabled = true;
    };

    struct entry
    {
        ce::physics::body_definition definition;
        ce::physics::body_state state{};
        ce::physics::body_handle body;
        bool enabled = true;
        std::uint64_t publication = 0;
        std::size_t changed_index = 0;
        ce::physics::pose previous{};
        std::uint64_t pose_tick = 0;
        std::uint64_t prepared_tick = 0;
        std::size_t render_index = 0;
    };

    ce::physics::result<void> RequireOwner() const;
    ce::physics::result<void> RetireContactBody(ce::physics::body_handle body, binding_id id);
    ce::physics::result<ce::physics::body_handle> RequireActiveBody(binding_id binding) const;
    void Reset(entry& value);
    static void Reset(character_entry& value);
    ce::physics::result<void> MoveCharacters();
    ce::physics::result<character_entry*> ActiveCharacter(binding_id binding);
    static std::uint64_t Key(ce::physics::body_handle handle);

    std::thread::id m_owner = std::this_thread::get_id();
    std::unordered_map<binding_id, entry> m_entries;
    std::unordered_map<binding_id, character_entry> m_characters;
    std::vector<binding_id> m_characterOrder;
    std::vector<changed_character> m_changedCharacters;
    std::unordered_map<std::uint64_t, binding_id> m_handles;
    // Replaced actors may emit a final lost-pair event at the next fetch.
    std::unordered_map<std::uint64_t, binding_id> m_retiredContactHandles;
    std::vector<contact_event> m_contacts;
    std::vector<changed_pose> m_changed;
    std::vector<render_pose> m_render, m_nextRender;
    // Owner-only scratch; entry addresses are stable throughout one synchronous step.
    std::vector<entry*> m_renderPrepared;
    std::unique_ptr<ce::physics::PhysicsScene> m_runtime;
    binding_id m_next = 1;
    std::uint64_t m_publication = 0;
    std::uint64_t m_layerRevision = 0;
    double m_accumulator = 0, m_dropped = 0;
};
