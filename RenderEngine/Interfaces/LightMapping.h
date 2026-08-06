#pragma once
#include "Core.Minimal.h"
#include "LightMapping.generated.h"

struct LightMapping
{
    [[Property]]
    int lightmapIndex{ -1 };
    [[Property]]
    int ligthmapResolution{ 0 };
    [[Property]]
    float lightmapScale{ 1.f };
    [[Property]]
    Mathf::Vector2 lightmapOffset{ 0,0 };
    [[Property]]
    Mathf::Vector2 lightmapTiling{ 0,0 };

   ReflectLightMapping
    [[Serializable]]
    LightMapping() = default;
    ~LightMapping() = default;
};
