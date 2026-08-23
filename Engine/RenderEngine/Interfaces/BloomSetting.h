#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct BloomPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = BloomPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::applyBloom>,
           meta::field<&Self::threshold>,
           meta::field<&Self::knee>,
           meta::field<&Self::coefficient>,
           meta::field<&Self::blurRadius>,
           meta::field<&Self::blurSigma>);
   }
    BloomPassSetting() = default;

    bool applyBloom{ true };
    float threshold{ 5.f };
    float knee{ 0.3f };
    float coefficient{ 0.05f };
    int blurRadius{ 3 };
    float blurSigma{ 2.f };
};
