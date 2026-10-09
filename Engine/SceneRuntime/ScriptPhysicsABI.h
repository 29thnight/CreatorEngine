#pragma once

#include "../Physics/PhysicsGeometry.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ce::script
{
struct vector3
{
    float x, y, z;
};
struct vector4
{
    float x, y, z, w;
};

inline constexpr int physics_query_capacity = 256;
inline constexpr int PhysicsStatus(ce::physics::error_code code) noexcept
{
    return static_cast<int>(code) + 1; // Zero is success; the managed enum mirrors this mapping.
}

struct physics_body_state
{
    std::int32_t kind;
    float mass;
    vector3 position;
    vector4 rotation;
    vector3 linear_velocity, angular_velocity;
};

struct physics_character_state
{
    vector3 position, foot_position, actual_displacement, desired_velocity;
    float fall_velocity;
    std::uint32_t flags; // sides=1, above=2, below=4, simulating=8.
    std::uint64_t tick;
    vector3 movement_velocity;
    double forced_remaining;
};

struct physics_shape_state
{
    std::uint32_t id;
    std::int32_t kind, sensor, query_enabled;
    std::uint64_t layer_override;
    float radius, half_height;
    vector3 half_extent, local_position;
    vector4 local_rotation;
    ce::physics::contact_role_id role;
};

struct physics_hit
{
    std::uint32_t object_index, object_generation;
    std::uint64_t component;
    std::uint32_t shape, face;
    std::uint64_t layer;
    vector3 point, normal;
    float distance;
    std::int32_t has_location;
};

struct physics_query_result
{
    std::int32_t written, required_capacity, truncated;
};

struct physics_query_request
{
    std::int32_t kind;
    vector3 origin, direction;
    float distance_or_radius;
    std::uint32_t layers;
    std::int32_t include_sensors, offset, capacity;
};

struct physics_batch_result
{
    std::int32_t error, written, required_capacity, truncated;
};

inline constexpr int physics_batch_capacity = 64;
inline constexpr int physics_batch_hit_capacity = 4096;
static_assert(sizeof(physics_query_request) == 48 && offsetof(physics_query_request, offset) == 40);
static_assert(sizeof(physics_batch_result) == 16);

static_assert(sizeof(vector3) == 12 && sizeof(vector4) == 16);
static_assert(sizeof(physics_body_state) == 60);
static_assert(sizeof(physics_character_state) == 88 && offsetof(physics_character_state, tick) == 56);
static_assert(offsetof(physics_character_state, forced_remaining) == 80);
static_assert(sizeof(physics_shape_state) == 88);
static_assert(sizeof(physics_hit) == 64 && offsetof(physics_hit, component) == 8);
static_assert(offsetof(physics_hit, layer) == 24 && offsetof(physics_hit, point) == 32);
static_assert(sizeof(physics_query_result) == 12);
static_assert(std::is_trivially_copyable_v<physics_hit>);
} // namespace ce::script
