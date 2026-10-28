#pragma once
#include "../../Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Foundation/Core.Minimal.h"

struct [[reflgen::reflect, reflgen::public_methods]] BloomPassSetting
{
   public:
    BloomPassSetting() = default;

    bool applyBloom{ true };
    float threshold{ 5.f };
    float knee{ 0.3f };
    float coefficient{ 0.05f };
    int blurRadius{ 3 };
    float blurSigma{ 2.f };
};
