#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"

struct VolumeProfile
{
    static consteval auto describe()
    {
        return meta::describe<VolumeProfile>(
            meta::member<&VolumeProfile::settings>());
    }
    VolumeProfile() = default;

    RenderPassSettings settings{};
};
