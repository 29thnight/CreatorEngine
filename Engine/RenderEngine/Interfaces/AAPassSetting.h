#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] AAPassSetting
{
   public:
    AAPassSetting() = default;
   ~AAPassSetting() = default;

    bool isApply{ true };
    float bias{ 0.688f };
    float biasMin{ 0.021f };
    float spanMax{ 8.0f };
};
