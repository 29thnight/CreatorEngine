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
    float threshold{ 5.f };
    [[Property]]
    float knee{ 0.3f };
    [[Property]]
    float coefficient{ 0.05f };
    [[Property]]
    int blurRadius{ 3 };
    [[Property]]
    float blurSigma{ 2.f };
};
