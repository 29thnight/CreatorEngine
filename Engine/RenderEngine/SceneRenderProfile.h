#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"

struct [[reflgen::reflect]] SceneRenderProfile
{
  public:
    SceneRenderProfile() = default;

    RenderPassSettings settings{};
};
