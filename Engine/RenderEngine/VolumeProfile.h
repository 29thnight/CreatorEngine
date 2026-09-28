#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "RenderPassSettings.h"

struct [[reflgen::reflect]] VolumeProfile
{
    public:
    VolumeProfile() = default;

    RenderPassSettings settings{};
};
