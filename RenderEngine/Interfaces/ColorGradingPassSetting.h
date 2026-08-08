#pragma once
#include "Core.Minimal.h"
#include "ColorGradingPassSetting.generated.h"

struct ColorGradingPassSetting
{
   ReflectColorGradingPassSetting
    [[Serializable]]
    ColorGradingPassSetting() = default;

	[[Property]]
    bool isOn{ true };
	[[Property]]
    float lerp{ 0.f };
    [[Property]]
    HashingString textureFilePath{"None"};
};
