#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct VignettePassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = VignettePassSetting;
       return meta::schema<Self>(
           meta::field<&Self::isOn>,
           meta::field<&Self::radius>,
           meta::field<&Self::softness>);
   }
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};
