#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct DeferredPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<DeferredPassSetting>(
           meta::member<&DeferredPassSetting::useAmbientOcclusion>(),
           meta::member<&DeferredPassSetting::useEnvironmentMap>(),
           meta::member<&DeferredPassSetting::useLightWithShadows>(),
           meta::member<&DeferredPassSetting::envMapIntensity>());
   }
    DeferredPassSetting() = default;

    bool useAmbientOcclusion{ true };
    bool useEnvironmentMap{ true };
    bool useLightWithShadows{ true };
    float envMapIntensity{ 1.f };
};
