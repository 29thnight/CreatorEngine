#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ColorGradingPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<ColorGradingPassSetting>(
           meta::member<&ColorGradingPassSetting::isOn>(),
           meta::member<&ColorGradingPassSetting::lerp>(),
           meta::member<&ColorGradingPassSetting::textureFilePath>());
   }
    ColorGradingPassSetting() = default;

    bool isOn{ true };
    float lerp{ 0.f };
    HashingString textureFilePath{"None"};
};
