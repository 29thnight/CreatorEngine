#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/EngineStorage.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Layers.h"
#else
import Creator.Core.Layers;
#endif
#include "SimulationTick.h"
#include "../Physics/PhysicsScene.h"
#include "ProjectLayerSettings.h"
#include "CharacterMotionPolicy.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"

// Scene-owned runtime. Registration is authoring membership; no SDK scene exists in editor mode.
class ScenePhysicsSimulation final
{
  public:
    using binding_id = std::uint64_t;

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] character_definition
    {
        ce::physics::character_desc capsule;
        math::vector3 initial_velocity{};
        float gravity = -9.81f;
        float initial_fall_velocity = 0;
        float minimum_distance = .001f;
        ce::physics::character_motion_parameters motion;
        ce::physics::character_motion_memory initial_motion;
    };

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] character_motion_state
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
    ce::physics::result<math::vector3> RenderCharacterPosition(binding_id binding) const;
    ce::physics::character_handle CharacterHandle(binding_id binding) const;
    ce::physics::result<void> Define(binding_id binding, ce::physics::body_definition definition);
    ce::physics::result<void> SetEnabled(binding_id binding, bool enabled);

    // Explicit runtime replacement, owner/idle only. Preserve pose/velocities and enabled membership.
    ce::physics::result<void> Replace(binding_id binding, ce::physics::body_definition definition);

    ce::physics::result<void> Start(const ce::physics::scene_config& config = {});
    ce::physics::result<void> Stop();

    ce::physics::result<std::uint32_t> Advance(double frame_seconds);
    // Production clock lives in SceneManager; this entry never accumulates time.
    ce::physics::result<std::uint32_t> Step();
    void SetInterpolationAlpha(double alpha) noexcept { m_externalAlpha = std::clamp(alpha, 0.0, 1.0); }

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
    double InterpolationAlpha() const noexcept { return m_externalAlpha >= 0 ? m_externalAlpha : std::clamp(m_accumulator / fixed_seconds, 0.0, 1.0); }

    ce::physics::result<void> QueryBatch(std::span<const ce::physics::query_request> requests,
                                        std::span<ce::physics::result<ce::physics::query_result>> results);

    bool IsRunning() const noexcept { return bool(m_runtime); }
    std::span<const changed_pose> ChangedPoses() const noexcept { return m_changed; }
    std::span<const changed_character> ChangedCharacters() const noexcept { return m_changedCharacters; }
    ce::physics::PhysicsScene* Runtime() const noexcept { return m_runtime.get(); }
    std::uint64_t CompletedSteps() const noexcept { return m_completedSteps; }
    double DroppedSeconds() const noexcept { return m_dropped; }

    static constexpr double fixed_seconds = Runtime::SimulationTick::seconds;
    static constexpr std::uint32_t max_catchup_ticks = Runtime::SimulationTick::max_catchup;

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
        math::vector3 previous{};
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

    ce::physics::result<std::uint32_t> AdvanceInternal(double seconds, bool singleStep);
    ce::physics::result<void> RequireOwner() const;
    ce::physics::result<void> RetireContactBody(ce::physics::body_handle body, binding_id id);
    ce::physics::result<ce::physics::body_handle> RequireActiveBody(binding_id binding) const;
    void Reset(entry& value);
    static void Reset(character_entry& value);
    ce::physics::result<void> MoveCharacters();
    ce::physics::result<character_entry*> ActiveCharacter(binding_id binding);
    static std::uint64_t Key(ce::physics::body_handle handle);

    std::thread::id m_owner = std::this_thread::get_id();
    ce::engine_extractable_map<binding_id, entry> m_entries;
    ce::engine_extractable_map<binding_id, character_entry> m_characters;
    ce::dynamic_array<binding_id> m_characterOrder;
    ce::dynamic_array<changed_character> m_changedCharacters;
    ce::engine_extractable_map<std::uint64_t, binding_id> m_handles;
    // Replaced actors may emit a final lost-pair event at the next fetch.
    ce::engine_extractable_map<std::uint64_t, binding_id> m_retiredContactHandles;
    ce::dynamic_array<contact_event> m_contacts;
    ce::dynamic_array<changed_pose> m_changed;
    ce::dynamic_array<render_pose> m_render, m_nextRender;
    // Owner-only scratch; entry addresses are stable throughout one synchronous step.
    ce::dynamic_array<entry*> m_renderPrepared;
    std::unique_ptr<ce::physics::PhysicsScene> m_runtime;
    binding_id m_next = 1;
    std::uint64_t m_publication = 0;
    std::uint64_t m_completedSteps = 0;
    std::uint64_t m_layerRevision = 0;
    double m_accumulator = 0, m_dropped = 0;
    double m_externalAlpha = -1;
};
