#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct BloomPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<BloomPassSetting>(
           meta::member<&BloomPassSetting::applyBloom>(),
           meta::member<&BloomPassSetting::threshold>(),
           meta::member<&BloomPassSetting::knee>(),
           meta::member<&BloomPassSetting::coefficient>(),
           meta::member<&BloomPassSetting::blurRadius>(),
           meta::member<&BloomPassSetting::blurSigma>());
   }
    BloomPassSetting() = default;

    bool applyBloom{ true };
    float threshold{ 5.f };
    float knee{ 0.3f };
    float coefficient{ 0.05f };
    int blurRadius{ 3 };
    float blurSigma{ 2.f };
};
