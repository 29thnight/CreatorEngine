#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class BillboardType : std::uint8_t
{
    None = 0,
    Spherical = 1,
    Cylindrical = 2
};
