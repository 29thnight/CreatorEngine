#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"

struct SceneRenderProfile
{
  public:
    static consteval auto reflect()
    {
        using Self = SceneRenderProfile;
        return meta::schema<Self>(meta::field<&Self::settings>);
    }
    SceneRenderProfile() = default;

    RenderPassSettings settings{};
};
