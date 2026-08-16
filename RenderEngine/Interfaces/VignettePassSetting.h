#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct VignettePassSetting
{
   static consteval auto describe()
   {
       return meta::describe<VignettePassSetting>(
           meta::member<&VignettePassSetting::isOn>(),
           meta::member<&VignettePassSetting::radius>(),
           meta::member<&VignettePassSetting::softness>());
   }
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};
