#pragma once
#include "../Physics/PhysicsCharacter.h"
#include <algorithm>
#include <cmath>

namespace ce::physics
{
struct character_motion_parameters
{
    float acceleration = 0;    // m/s²; zero selects direct input, bypassing acceleration and braking.
    float braking_decay = 0;   // 1/s exponential decay; zero means no decay in accelerated mode.
    float jump_speed = 5;      // m/s impulse, grounded only.
    float max_fall_speed = 55; // Positive downward speed limit, m/s.
};

struct character_motion_memory
{
    math::vector3 velocity{}, forced_velocity{};
    double forced_remaining = 0; // Simulation seconds, not render/wall-clock time.
    bool jump_requested = false;
};

inline bool finite_motion_vector(math::vector3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool valid_motion_parameters(const character_motion_parameters& value)
{
    return std::isfinite(value.acceleration) && value.acceleration >= 0 && std::isfinite(value.braking_decay) &&
           value.braking_decay >= 0 && std::isfinite(value.jump_speed) && value.jump_speed > 0 &&
           std::isfinite(value.max_fall_speed) && value.max_fall_speed > 0;
}

inline bool valid_motion_memory(const character_motion_memory& value)
{
    return finite_motion_vector(value.velocity) && finite_motion_vector(value.forced_velocity) &&
           std::isfinite(value.forced_remaining) && value.forced_remaining >= 0;
}

struct character_motion_step
{
    character_motion_memory motion;
    math::vector3 displacement{};
    float fall_velocity = 0;
};

// Pure fixed-step preparation: no SDK mutation, allocation, or wall-clock dependence.
inline result<character_motion_step> prepare_character_motion(const character_motion_parameters& parameters,
                                                              character_motion_memory motion, math::vector3 desired,
                                                              float fall_velocity, float gravity, double seconds)
{
    if (!valid_motion_parameters(parameters) || !valid_motion_memory(motion) || !finite_motion_vector(desired) ||
        !std::isfinite(fall_velocity) || !std::isfinite(gravity) || !std::isfinite(seconds) || seconds <= 0)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character motion input"});

    if (parameters.acceleration == 0)
        motion.velocity = desired;
    else if (desired == math::vector3{})
        motion.velocity *= static_cast<float>(std::exp(-static_cast<double>(parameters.braking_decay) * seconds));
    else
    {
        const double x = static_cast<double>(desired.x) - motion.velocity.x;
        const double y = static_cast<double>(desired.y) - motion.velocity.y;
        const double z = static_cast<double>(desired.z) - motion.velocity.z;
        const double distance = std::hypot(x, y, z);
        const double ratio = distance == 0 ? 1 : std::min(1.0, parameters.acceleration * seconds / distance);
        motion.velocity = {static_cast<float>(motion.velocity.x + x * ratio),
                           static_cast<float>(motion.velocity.y + y * ratio),
                           static_cast<float>(motion.velocity.z + z * ratio)};
    }

    const double start_fall = motion.jump_requested ? parameters.jump_speed : fall_velocity;
    const double fall = std::max(-static_cast<double>(parameters.max_fall_speed), start_fall + gravity * seconds);
    motion.jump_requested = false;
    const double forced_seconds = std::min(motion.forced_remaining, seconds);
    math::vector3 displacement{
        static_cast<float>(motion.forced_velocity.x * forced_seconds + motion.velocity.x * (seconds - forced_seconds)),
        static_cast<float>(motion.forced_velocity.y * forced_seconds + motion.velocity.y * (seconds - forced_seconds) +
                           fall * seconds),
        static_cast<float>(motion.forced_velocity.z * forced_seconds + motion.velocity.z * (seconds - forced_seconds))};
    motion.forced_remaining = std::max(0.0, motion.forced_remaining - seconds);
    if (motion.forced_remaining == 0)
        motion.forced_velocity = {};

    if (!finite_motion_vector(motion.velocity) || !finite_motion_vector(displacement) ||
        !std::isfinite(static_cast<float>(fall)))
        return std::unexpected(error{error_code::invalid_argument, 0, "Character motion integration overflow"});

    return character_motion_step{motion, displacement, static_cast<float>(fall)};
}
} // namespace ce::physics
