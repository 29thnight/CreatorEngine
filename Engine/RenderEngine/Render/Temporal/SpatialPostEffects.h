#pragma once
#include "TemporalReconstruction.h"
#include <cmath>

// Separate from temporal reconstruction: NIS never supplies motion/history AA.
enum class SpatialScalingMode : uint8_t { Off, NisScale, NisSharpen };

struct SpatialPostSettings
{
    SpatialScalingMode nisMode{ SpatialScalingMode::Off };
    float nisRenderScale{ 0.77f };
    float nisSharpness{ 0.5f };
    bool deepDvcEnabled{ false };
    float deepDvcIntensity{ 0.5f };
    float deepDvcSaturationBoost{ 0.25f };
    bool operator==(const SpatialPostSettings&) const = default;
};

struct SpatialPostSnapshot
{
    TemporalResult nisCapability, deepDvcCapability, nisResult, deepDvcResult;
    SpatialScalingMode selectedNisMode{ SpatialScalingMode::Off };
    SpatialScalingMode activeNisMode{ SpatialScalingMode::Off };
    bool deepDvcSelected{ false }, deepDvcApplied{ false };
    bool sdrEligible{ false }, observed{ false };
    uint64_t realFrameId{ 0 };
    uint64_t generation{ 0 };
    SpatialPostSettings effectiveSettings;
    TemporalExtent inputExtent, outputExtent;
};

inline bool ValidateSpatialPostSettings(const SpatialPostSettings& settings)
{
    const auto unit = [](float value)
    {
        return std::isfinite(value) && value >= 0.f && value <= 1.f;
    };
    return settings.nisMode <= SpatialScalingMode::NisSharpen &&
        std::isfinite(settings.nisRenderScale) && settings.nisRenderScale >= 0.5f && settings.nisRenderScale <= 1.f &&
        unit(settings.nisSharpness) && unit(settings.deepDvcIntensity) && unit(settings.deepDvcSaturationBoost);
}

inline const char* SpatialScalingModeName(SpatialScalingMode mode)
{
    switch (mode)
    {
    case SpatialScalingMode::NisScale: return "scale";
    case SpatialScalingMode::NisSharpen: return "sharpen";
    default: return "off";
    }
}
