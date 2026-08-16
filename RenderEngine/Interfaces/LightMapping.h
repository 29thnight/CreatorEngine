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

   // CT4 파일럿 — 부모 없는 구조체 케이스(Parent = void).
   static consteval auto reflect()
   {
       return meta::describe<LightMapping, void,
           &LightMapping::lightmapIndex,
           &LightMapping::ligthmapResolution,
           &LightMapping::lightmapScale,
           &LightMapping::lightmapOffset,
           &LightMapping::lightmapTiling>{};
   }
   static const Meta::Type& Reflect() { return meta::adapt<LightMapping>(); }

    LightMapping() = default;
    ~LightMapping() = default;
};
