#pragma once
#include "Core.Minimal.h"
#include "VignettePassSetting.generated.h"

struct VignettePassSetting
{
   ReflectVignettePassSetting
    [[Serializable]]
    VignettePassSetting() = default;

	[[Property]]
    bool isOn{ true };
	[[Property]]
    float radius{ 0.75f };
	[[Property]]
    float softness{ 0.5f };
};
