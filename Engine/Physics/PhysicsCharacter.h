#pragma once
#include "PhysicsGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ce::physics
{
struct character_desc
{
    math::vector3 position{0, 2, 0}; // Capsule center, metres; local up is +Y.
    float radius = .5f;
    float cylinder_height = 1.f; // Excludes the two hemispheres.
    float contact_offset = .05f;
    float step_offset = .3f;
    float slope_limit_cosine = .70710678f; // 0 disables slope limiting; otherwise cosine, not degrees.
    std::uint32_t belongs_to = 1;
    std::uint32_t collides_with = UINT32_MAX;
};

inline bool valid_character_desc(const character_desc& value)
{
    const std::array scalars{value.position.x,      value.position.y,     value.position.z,  value.radius,
                             value.cylinder_height, value.contact_offset, value.step_offset, value.slope_limit_cosine};
    return std::ranges::all_of(scalars, [](float scalar) { return std::isfinite(scalar); }) && value.radius > 0 &&
           value.cylinder_height > 0 && value.contact_offset > 0 && value.step_offset >= 0 &&
           value.step_offset <= value.cylinder_height + 2 * value.radius &&
           std::isfinite(value.cylinder_height + 2 * value.radius) && value.slope_limit_cosine >= 0 &&
           value.slope_limit_cosine <= 1;
}

struct character_move
{
    math::vector3 displacement; // Metres for this move, not metres/second.
    float seconds = 1.f / 60;
    float minimum_distance = .001f;
};

struct character_state
{
    math::vector3 position, foot_position;
    math::vector3 actual_displacement;
    bool sides = false, above = false, below = false; // Flags of the last move; not a persistent ground probe.
};

struct character_pose
{
    character_handle character;
    character_state state;
};
} // namespace ce::physics
