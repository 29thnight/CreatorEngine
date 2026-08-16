#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct SSGIPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<SSGIPassSetting>(
           meta::member<&SSGIPassSetting::isOn>(),
           meta::member<&SSGIPassSetting::useOnlySSGI>(),
           meta::member<&SSGIPassSetting::useDualFilteringStep>(),
           meta::member<&SSGIPassSetting::radius>(),
           meta::member<&SSGIPassSetting::thickness>(),
           meta::member<&SSGIPassSetting::intensity>(),
           meta::member<&SSGIPassSetting::ssratio>());
   }
    SSGIPassSetting() = default;

    bool isOn{ true };
    bool useOnlySSGI{ false };
    int useDualFilteringStep{ 2 };
    float radius{ 4.f };
    float thickness{ 0.5f };
    float intensity{ 1.f };
    int ssratio{ 4 };
};
