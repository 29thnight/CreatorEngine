#pragma once
#include "Core.Minimal.h"
#include "BloomPassSetting.generated.h"

struct BloomPassSetting
{
   ReflectBloomPassSetting
    [[Serializable]]
    BloomPassSetting() = default;

	[[Property]]
    bool applyBloom{ true };
    [[Property]]
    float threshold{ 0.3f };
    [[Property]]
    float knee{ 0.5f };
    [[Property]]
    float coefficient{ 0.3f };
    [[Property]]
    int blurRadius{ 7 };
    [[Property]]
    float blurSigma{ 5.f };
};
