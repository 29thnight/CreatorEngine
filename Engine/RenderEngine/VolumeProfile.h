#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"

struct VolumeProfile
{
    public:
    static consteval auto reflect()
    {
        using Self = VolumeProfile;
        return meta::schema<Self>(
            meta::field<&Self::settings>);
    }
    VolumeProfile() = default;

    RenderPassSettings settings{};
};
