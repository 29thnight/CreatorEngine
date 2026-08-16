#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct SSAOPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<SSAOPassSetting>(
           meta::member<&SSAOPassSetting::radius>(),
           meta::member<&SSAOPassSetting::thickness>());
   }
    SSAOPassSetting() = default;

    float radius{ 0.1f };
    float thickness{ 0.1f };
};
