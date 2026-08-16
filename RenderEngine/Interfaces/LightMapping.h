#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct LightMapping
{
    int lightmapIndex{ -1 };
    int ligthmapResolution{ 0 };
    float lightmapScale{ 1.f };
    Mathf::Vector2 lightmapOffset{ 0,0 };
    Mathf::Vector2 lightmapTiling{ 0,0 };

   // CT4 파일럿 — 부모 없는 구조체 케이스.
   ReflectionMetaField(LightMapping,
       ct_property(lightmapIndex),
       ct_property(ligthmapResolution),
       ct_property(lightmapScale),
       ct_property(lightmapOffset),
       ct_property(lightmapTiling))

    LightMapping() = default;
    ~LightMapping() = default;
};
