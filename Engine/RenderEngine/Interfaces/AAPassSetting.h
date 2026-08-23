#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct AAPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = AAPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::isApply>,
           meta::field<&Self::bias>,
           meta::field<&Self::biasMin>,
           meta::field<&Self::spanMax>);
   }
    AAPassSetting() = default;
   ~AAPassSetting() = default;

    bool isApply{ true };
    float bias{ 0.688f };
    float biasMin{ 0.021f };
    float spanMax{ 8.0f };
};
