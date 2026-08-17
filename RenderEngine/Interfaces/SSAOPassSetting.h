#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct SSAOPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = SSAOPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::radius>,
           meta::field<&Self::thickness>);
   }
    SSAOPassSetting() = default;

    float radius{ 0.1f };
    float thickness{ 0.1f };
};
