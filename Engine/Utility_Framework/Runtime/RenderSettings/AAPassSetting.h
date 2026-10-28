#pragma once
#include "../../Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Foundation/Core.Minimal.h"

struct [[reflgen::reflect, reflgen::public_methods]] AAPassSetting
{
   public:
    AAPassSetting() = default;
   ~AAPassSetting() = default;

    bool isApply{ true };
    float bias{ 0.688f };
    float biasMin{ 0.021f };
    float spanMax{ 8.0f };
};
