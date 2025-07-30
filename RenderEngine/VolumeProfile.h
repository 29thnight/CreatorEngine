#pragma once
#include "RenderPassSettings.h"
#include "VolumeProfile.generated.h"

struct VolumeProfile
{
    ReflectVolumeProfile
    [[Serializable]]
    VolumeProfile() = default;

    [[Property]]
	std::string profileName{};
    [[Property]]
    RenderPassSettings settings{};
};
