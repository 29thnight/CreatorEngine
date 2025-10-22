#pragma once
#include "Core.Minimal.h"

enum class BillboardType : std::uint8_t
{
    None = 0,
    Spherical = 1,
    Cylindrical = 2
};
AUTO_REGISTER_ENUM(BillboardType);
