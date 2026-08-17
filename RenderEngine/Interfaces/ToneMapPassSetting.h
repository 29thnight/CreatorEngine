#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ToneMapPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = ToneMapPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::isAbleAutoExposure>,
           meta::field<&Self::isAbleToneMap>,
           meta::field<&Self::fNumber>,
           meta::field<&Self::shutterTime>,
           meta::field<&Self::ISO>,
           meta::field<&Self::exposureCompensation>,
           meta::field<&Self::speedBrightness>,
           meta::field<&Self::speedDarkness>,
           meta::field<&Self::toneMapType>,
           meta::field<&Self::filmSlope>,
           meta::field<&Self::filmToe>,
           meta::field<&Self::filmShoulder>,
           meta::field<&Self::filmBlackClip>,
           meta::field<&Self::filmWhiteClip>,
           meta::field<&Self::toneMapExposure>);
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
