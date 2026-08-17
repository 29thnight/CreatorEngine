#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct SSGIPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = SSGIPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::isOn>,
           meta::field<&Self::useOnlySSGI>,
           meta::field<&Self::useDualFilteringStep>,
           meta::field<&Self::radius>,
           meta::field<&Self::thickness>,
           meta::field<&Self::intensity>,
           meta::field<&Self::ssratio>);
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
