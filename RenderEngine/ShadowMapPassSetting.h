#pragma once
#include "Core.Minimal.h"
#include "ShadowMapPassSetting.generated.h"

struct ShadowMapPassSetting
{
   ReflectShadowMapPassSetting
    [[Serializable]]
    ShadowMapPassSetting() = default;

	[[Property]]
    bool useCascade{ true };
    [[Property]]
    bool isCloudOn{ true };
    [[Property]]
    Mathf::Vector2 cloudSize{ 7.f, 7.f };
    [[Property]]
    Mathf::Vector2 cloudDirection{ 1.f, 1.f };
    [[Property]]
    float cloudMoveSpeed{ 0.00006f };
    [[Property]]
    float epsilon{ 0.001f };
};
