#pragma once
#include <mathematics/vector2.hpp>
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] LightMapping
{
   public:
    int lightmapIndex{ -1 };
    int ligthmapResolution{ 0 };
    float lightmapScale{ 1.f };
    math::vector2 lightmapOffset{ 0,0 };
    math::vector2 lightmapTiling{ 0,0 };


    LightMapping() = default;
    ~LightMapping() = default;
};
