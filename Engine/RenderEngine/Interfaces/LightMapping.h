#pragma once
#include <mathematics/vector2.hpp>
#include "../../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Utility_Framework/Foundation/Core.Minimal.h"

struct [[reflgen::reflect, reflgen::public_methods]] LightMapping
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
