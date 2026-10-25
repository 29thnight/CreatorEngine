#pragma once
#include "../../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Utility_Framework/Foundation/Core.Minimal.h"

struct [[reflgen::reflect]] DeferredPassSetting
{
   public:
    DeferredPassSetting() = default;

    bool useAmbientOcclusion{ true };
    bool useEnvironmentMap{ true };
    bool useLightWithShadows{ true };
    float envMapIntensity{ 1.f };
};
