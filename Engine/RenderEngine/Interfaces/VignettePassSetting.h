#pragma once
#include "../../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Utility_Framework/Foundation/Core.Minimal.h"

struct [[reflgen::reflect]] VignettePassSetting
{
   public:
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};
