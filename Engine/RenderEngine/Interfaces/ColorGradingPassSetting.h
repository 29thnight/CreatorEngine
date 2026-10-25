#pragma once
#include "../../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Utility_Framework/Foundation/Core.Minimal.h"

struct [[reflgen::reflect]] ColorGradingPassSetting
{
   public:
    ColorGradingPassSetting() = default;

    bool isOn{ true };
    float lerp{ 0.f };
    std::string textureFilePath{"None"};
};
