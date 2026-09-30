#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] SSAOPassSetting
{
   public:
    SSAOPassSetting() = default;

    float radius{ 0.1f };
    float thickness{ 0.1f };
};
