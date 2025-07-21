#pragma once
#include "Core.Minimal.h"
#include "SSGIPassSetting.generated.h"

struct SSGIPassSetting
{
   ReflectSSGIPassSetting
    [[Serializable]]
    SSGIPassSetting() = default;

	[[Property]]
    bool isOn{ true };
    [[Property]]
    bool useOnlySSGI{ false };
    [[Property]]
    int useDualFilteringStep{ 2 };
	[[Property]]
    float radius{ 4.f };
    [[Property]]
    float thickness{ 0.5f };
	[[Property]]
    float intensity{ 1.f };
	[[Property]]
    int ssratio{ 4 };
};
