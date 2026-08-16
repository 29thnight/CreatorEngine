#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct AAPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<AAPassSetting>(
           meta::member<&AAPassSetting::isApply>(),
           meta::member<&AAPassSetting::bias>(),
           meta::member<&AAPassSetting::biasMin>(),
           meta::member<&AAPassSetting::spanMax>());
   }
    AAPassSetting() = default;
   ~AAPassSetting() = default;

    bool isApply{ true };
    float bias{ 0.688f };
    float biasMin{ 0.021f };
    float spanMax{ 8.0f };
};
