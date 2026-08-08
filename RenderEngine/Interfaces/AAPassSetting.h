#pragma once
#include "Core.Minimal.h"
#include "AAPassSetting.generated.h"

struct AAPassSetting
{
   ReflectAAPassSetting
    [[Serializable]]
    AAPassSetting() = default;
   ~AAPassSetting() = default;

    [[Property]]
    bool isApply{ true };
    [[Property]]
    float bias{ 0.688f };
    [[Property]]
    float biasMin{ 0.021f };
    [[Property]]
    float spanMax{ 8.0f };
};
