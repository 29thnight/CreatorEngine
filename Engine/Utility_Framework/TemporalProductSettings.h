#pragma once

#include <cmath>
#include <cstdint>
#include <string>

// Portable project intent. Device capabilities, native runtime directories and
// application credentials/SDK bootstrap identity never belong in this asset.
// SceneRenderProfile continues to own scene pass parameters; these defaults
// must be available before a scene or graphics device exists.
struct TemporalProductSettings
{
    std::uint32_t schemaVersion{ 1 };
    bool enabled{ true };
    std::string upscaler{ "none" };
    std::string quality{ "quality" };
    std::string frameGenerator{ "none" };
    std::uint32_t interpolatedFrameCount{ 1 };
    std::string latencyMode{ "off" };
    bool fallbackAa{ true };
    std::string spatialMode{ "off" };
    float spatialRenderScale{ 0.77f };
    float spatialSharpness{ 0.5f };
    bool digitalVibrance{ false };
    float vibranceIntensity{ 0.5f };
    float saturationBoost{ 0.25f };
    bool operator==(const TemporalProductSettings&) const = default;
};

inline bool ValidateTemporalProductSettings(const TemporalProductSettings& settings)
{
    const auto provider = [](const std::string& value)
    {
        return value == "none" || value == "fsr" || value == "dlss" || value == "xess";
    };
    const auto unit = [](float value)
    {
        return std::isfinite(value) && value >= 0.f && value <= 1.f;
    };
    return settings.schemaVersion == 1 && provider(settings.upscaler) && provider(settings.frameGenerator) &&
        (settings.quality == "native-aa" || settings.quality == "quality" || settings.quality == "balanced" ||
            settings.quality == "performance" || settings.quality == "ultra-performance") &&
        settings.interpolatedFrameCount != 0 &&
        (settings.latencyMode == "off" || settings.latencyMode == "on" || settings.latencyMode == "on-boost") &&
        (settings.spatialMode == "off" || settings.spatialMode == "scale" || settings.spatialMode == "sharpen") &&
        std::isfinite(settings.spatialRenderScale) && settings.spatialRenderScale >= 0.5f &&
        settings.spatialRenderScale <= 1.f && unit(settings.spatialSharpness) &&
        unit(settings.vibranceIntensity) && unit(settings.saturationBoost);
}
