#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] ColorGradingPassSetting
{
   public:
    ColorGradingPassSetting() = default;

    bool isOn{ true };
    float lerp{ 0.f };
    HashingString textureFilePath{"None"};
};
