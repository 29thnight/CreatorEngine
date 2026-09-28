#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] ToneMapPassSetting
{
   public:
    ToneMapPassSetting() = default;

    bool isAbleAutoExposure{ true };
    bool isAbleToneMap{ true };
    float fNumber{ 4.85f };
    float shutterTime{ 16.f };
    float ISO{ 75.f };
    float exposureCompensation{ 0.2f };
    float speedBrightness{ 0.002f };
    float speedDarkness{ 0.002f };
    int toneMapType{ 1 };
    float filmSlope{ 0.88f };
    float filmToe{ 0.55f };
    float filmShoulder{ 0.26f };
    float filmBlackClip{ 0.f };
    float filmWhiteClip{ 0.04f };
    float toneMapExposure{ 1.f };
};
