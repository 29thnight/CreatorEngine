#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ColorGradingPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = ColorGradingPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::isOn>,
           meta::field<&Self::lerp>,
           meta::field<&Self::textureFilePath>);
   }
    ColorGradingPassSetting() = default;

    bool isOn{ true };
    float lerp{ 0.f };
    HashingString textureFilePath{"None"};
};
