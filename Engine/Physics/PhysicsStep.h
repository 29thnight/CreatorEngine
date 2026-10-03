#pragma once
#include "PhysicsGeometry.h"
#include "PhysicsCharacter.h"
#include <array>
#include <optional>
#include <new>
#include <tuple>
#include <vector>

namespace ce::physics
{
// The descriptor's shape span is never retained. view() always binds owned storage.
struct body_definition
{
    body_desc properties;
    std::vector<ShapeInstance> shapes;

    [[nodiscard]] static result<body_definition> copy(const body_desc& value,
                                                      std::source_location location = std::source_location::current())
    {
        try
        {
            body_definition output{value, {value.shapes.begin(), value.shapes.end()}};
            output.properties.shapes = {};
            return output;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                error{error_code::out_of_memory, 0, "Body command payload allocation failed", location});
        }
    }

    [[nodiscard]] body_desc view() const noexcept
    {
        auto output = properties;
        output.shapes = shapes;
        return output;
    }
};

struct create_body_command
{
    body_definition definition;
};

struct destroy_body_command
{
    body_handle body;
};

struct replace_body_command
{
    body_handle body;
    body_definition definition;
};

struct set_pose_command
{
    body_handle body;
    pose value;
};

struct set_velocity_command
{
    body_handle body;
    math::vector3 linear, angular;
};

struct set_kinematic_target_command
{
    body_handle body;
    pose value;
};

enum class force_mode : std::uint8_t
{
    force,
    impulse,
    acceleration,
    velocity_change
};

struct apply_force_command
{
    body_handle body;
    math::vector3 linear, angular;
    force_mode mode = force_mode::force;
};

struct create_character_command
{
    character_desc definition;
};

struct destroy_character_command
{
    character_handle character;
};

struct move_character_command
{
    character_handle character;
    character_move movement;
};

struct teleport_character_command
{
    character_handle character;
    math::vector3 position;
};

using command_payload =
    std::variant<create_body_command, destroy_body_command, set_pose_command, set_velocity_command,
                 set_kinematic_target_command, apply_force_command, replace_body_command, create_character_command,
                 destroy_character_command, move_character_command, teleport_character_command>;

struct command_stamp
{
    scene_id scene;
    tick_id tick;
    std::uint32_t producer = 0; // 0: main; 1: required AI; 2..255: explicit deterministic priority.
    std::uint64_t sequence = 0; // Nonzero, strictly increasing within a producer's committed history.

    [[nodiscard]] auto order() const noexcept { return std::tuple{tick.value, producer, sequence}; }
};

struct command
{
    command_stamp stamp;
    command_payload payload;

    command() = default;
    command(command_stamp identity, command_payload value) noexcept : stamp(identity), payload(std::move(value)) {}

    command(command&&) noexcept = default;
    command& operator=(command&&) noexcept = default;
    command(const command&) = delete;
    command& operator=(const command&) = delete;

  private:
    friend class PhysicsScene;
    friend class PhysicsSceneChannel;
    std::uint64_t m_received_tick = 0; // Overwritten by the accepting scene; never producer-supplied telemetry.
};

struct command_outcome
{
    command_stamp stamp;
    body_handle created;
    std::optional<error> failure;
    body_handle retired;    // Successful destroy/replace; created is the replacement handle.
    bool cancelled = false; // Terminal fetch failure cancels unapplied future requests.
    character_handle character_created, character_retired;
    std::uint64_t waited_ticks = 0;
};

enum class event_kind : std::uint8_t
{
    contact_begin,
    contact_persist,
    contact_end,
    sensor_enter,
    sensor_exit
};

struct event_endpoint
{
    body_handle body;
    shape_id shape;
    bool sensor = false;
};

struct contact_point
{
    math::vector3 position, normal, impulse; // Normal/impulse point from second toward first endpoint.
    float separation = 0;
};

inline constexpr std::size_t contact_point_capacity = 16;

struct collision_event
{
    event_kind kind;
    event_endpoint first, second;
    std::array<contact_point, contact_point_capacity> contacts{};
    std::uint32_t contact_count = 0;
    std::uint32_t required_contacts = 0; // Never silently treat a partial manifold as complete.
};

struct active_body_pose
{
    body_handle body;
    body_state state;
};

struct step_statistics
{
    std::uint64_t commands_applied = 0, commands_failed = 0;
    std::uint64_t commands_cancelled = 0;
    std::uint64_t required_events = 0, dropped_events = 0;
    std::uint64_t required_contacts = 0, dropped_contacts = 0;
    std::uint64_t unresolved_identities = 0;
    std::uint32_t snapshot_buffers = 0, snapshot_buffers_in_use = 0;
    std::uint64_t bodies = 0;
    std::uint64_t shapes = 0;
    std::uint64_t characters = 0;
    std::uint64_t active_bodies = 0;
    std::uint64_t active_shapes = 0;
    std::uint64_t changed_bodies = 0;
    std::uint64_t changed_shapes = 0;
    std::uint64_t changed_characters = 0;
    std::uint64_t commands_queued = 0;
    std::uint64_t events_stored = 0;
    std::uint64_t contacts_stored = 0;
    std::uint64_t queries = 0;
    std::uint64_t query_hits = 0;
    std::uint64_t query_overflows = 0;
    std::uint64_t workers = 0;
    std::uint64_t tasks_submitted = 0;
    std::uint64_t tasks_completed = 0;
    std::uint64_t tasks_inline = 0;
    std::uint64_t tick_buffer_bytes = 0;
    std::uint64_t tick_buffer_peak_bytes = 0;
    std::uint64_t query_scratch_peak_bytes = 0;
    std::uint64_t step_failed = 0;
    std::uint64_t command_rejections = 0;
    std::uint64_t command_overflows = 0;
    std::uint64_t max_command_wait_ticks = 0;
};

// All arrays and contact points are owned. No SDK or component pointers escape.
struct tick_snapshot
{
    scene_id scene;
    tick_id tick;
    bool step_succeeded = false;
    std::optional<error> failure;
    std::vector<command_outcome> commands;
    std::vector<collision_event> events;
    std::vector<active_body_pose> active_poses;
    std::vector<character_pose> characters;
    step_statistics statistics;
};

} // namespace ce::physics
