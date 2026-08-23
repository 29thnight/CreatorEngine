#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct LightMapping
{
   public:
   // CT4 파일럿 — 부모 없는 구조체 케이스(meta::base 생략).
   static consteval auto reflect()
   {
       using Self = LightMapping;
       return meta::schema<Self>(
           meta::field<&Self::lightmapIndex>,
           meta::field<&Self::ligthmapResolution>,
           meta::field<&Self::lightmapScale>,
           meta::field<&Self::lightmapOffset>,
           meta::field<&Self::lightmapTiling>);
   }
    int lightmapIndex{ -1 };
    int ligthmapResolution{ 0 };
    float lightmapScale{ 1.f };
    Mathf::Vector2 lightmapOffset{ 0,0 };
    Mathf::Vector2 lightmapTiling{ 0,0 };


    LightMapping() = default;
    ~LightMapping() = default;
};
