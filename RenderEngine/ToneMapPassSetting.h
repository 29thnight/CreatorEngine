#pragma once
#include "Core.Minimal.h"
#include "ToneMapPassSetting.generated.h"

struct ToneMapPassSetting
{
   ReflectToneMapPassSetting
    [[Serializable]]
    ToneMapPassSetting() = default;

   [[Property]]
    bool isAbleAutoExposure{ true };
    [[Property]]
    bool isAbleToneMap{ true };
    [[Property]]
    float fNumber{ 8.f };
    [[Property]]
    float shutterTime{ 8.f };
    [[Property]]
    float ISO{ 100.f };
    [[Property]]
    float exposureCompensation{ 0.f };
    [[Property]]
    float speedBrightness{ 3.f };
    [[Property]]
    float speedDarkness{ 1.7f };
    [[Property]]
    int toneMapType{ 1 };
    [[Property]]
    float filmSlope{ 0.88f };
    [[Property]]
    float filmToe{ 0.55f };
    [[Property]]
    float filmShoulder{ 0.26f };
    [[Property]]
    float filmBlackClip{ 0.f };
    [[Property]]
    float filmWhiteClip{ 0.04f };
    [[Property]]
    float toneMapExposure{ 1.f };
};
