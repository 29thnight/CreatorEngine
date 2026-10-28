#pragma once
#include "../../Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Foundation/Core.Minimal.h"

struct [[reflgen::reflect]] VignettePassSetting
{
   public:
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};
