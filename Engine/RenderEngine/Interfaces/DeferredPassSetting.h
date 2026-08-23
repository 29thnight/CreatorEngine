#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct DeferredPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = DeferredPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::useAmbientOcclusion>,
           meta::field<&Self::useEnvironmentMap>,
           meta::field<&Self::useLightWithShadows>,
           meta::field<&Self::envMapIntensity>);
   }
    DeferredPassSetting() = default;

    bool useAmbientOcclusion{ true };
    bool useEnvironmentMap{ true };
    bool useLightWithShadows{ true };
    float envMapIntensity{ 1.f };
};
