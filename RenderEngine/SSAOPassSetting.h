#pragma once
#include "Core.Minimal.h"
#include "SSAOPassSetting.generated.h"

struct SSAOPassSetting
{
   ReflectSSAOPassSetting
    [[Serializable]]
    SSAOPassSetting() = default;

	[[Property]]
    float radius{ 0.1f };
    [[Property]]
    float thickness{ 0.1f };
};
