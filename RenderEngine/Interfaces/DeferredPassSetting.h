#pragma once
#include "Core.Minimal.h"
#include "DeferredPassSetting.generated.h"

struct DeferredPassSetting
{
   ReflectDeferredPassSetting
    [[Serializable]]
    DeferredPassSetting() = default;

	[[Property]]
    bool useAmbientOcclusion{ true };
    [[Property]]
    bool useEnvironmentMap{ true };
    [[Property]]
    bool useLightWithShadows{ true };
    [[Property]]
    float envMapIntensity{ 1.f };
};
