#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] VignettePassSetting
{
   public:
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};
