#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"
#include "VolumeProfile.generated.h"

struct VolumeProfile
{
    ReflectVolumeProfile
    [[Serializable]]
    VolumeProfile() = default;

    [[Property]]
    RenderPassSettings settings{};
};
