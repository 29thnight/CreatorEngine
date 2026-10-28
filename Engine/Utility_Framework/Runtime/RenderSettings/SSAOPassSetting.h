#pragma once
#include "../../Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Foundation/Core.Minimal.h"

struct [[reflgen::reflect, reflgen::public_methods]] SSAOPassSetting
{
   public:
    SSAOPassSetting() = default;

    float radius{ 0.1f };
    float thickness{ 0.1f };
};
