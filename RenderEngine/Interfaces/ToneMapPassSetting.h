#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ToneMapPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<ToneMapPassSetting>(
           meta::member<&ToneMapPassSetting::isAbleAutoExposure>(),
           meta::member<&ToneMapPassSetting::isAbleToneMap>(),
           meta::member<&ToneMapPassSetting::fNumber>(),
           meta::member<&ToneMapPassSetting::shutterTime>(),
           meta::member<&ToneMapPassSetting::ISO>(),
           meta::member<&ToneMapPassSetting::exposureCompensation>(),
           meta::member<&ToneMapPassSetting::speedBrightness>(),
           meta::member<&ToneMapPassSetting::speedDarkness>(),
           meta::member<&ToneMapPassSetting::toneMapType>(),
           meta::member<&ToneMapPassSetting::filmSlope>(),
           meta::member<&ToneMapPassSetting::filmToe>(),
           meta::member<&ToneMapPassSetting::filmShoulder>(),
           meta::member<&ToneMapPassSetting::filmBlackClip>(),
           meta::member<&ToneMapPassSetting::filmWhiteClip>(),
           meta::member<&ToneMapPassSetting::toneMapExposure>());
   }
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
