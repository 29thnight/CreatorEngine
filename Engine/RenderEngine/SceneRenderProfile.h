#pragma once
#include "../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"

struct [[reflgen::reflect, reflgen::public_methods]] SceneRenderProfile
{
  public:
    SceneRenderProfile() = default;

    RenderPassSettings settings{};
};
