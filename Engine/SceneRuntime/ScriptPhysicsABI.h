#pragma once

#include "../Physics/PhysicsGeometry.h"
#include "ScriptPhysicsValues.h"

namespace ce::script
{
    inline constexpr int PhysicsStatus(ce::physics::error_code code) noexcept
    {
        return static_cast<int>(code) + 1; // Zero is success; the managed enum mirrors this mapping.
    }
}
