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

   // CT4 파일럿 — 부모 없는 구조체 케이스(meta::base 생략).
   static consteval auto describe()
   {
       return meta::describe<LightMapping>(
           meta::member<&LightMapping::lightmapIndex>(),
           meta::member<&LightMapping::ligthmapResolution>(),
           meta::member<&LightMapping::lightmapScale>(),
           meta::member<&LightMapping::lightmapOffset>(),
           meta::member<&LightMapping::lightmapTiling>());
   }

    LightMapping() = default;
    ~LightMapping() = default;
};
