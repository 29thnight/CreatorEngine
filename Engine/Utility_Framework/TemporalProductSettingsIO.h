#pragma once

#include "TemporalProductSettings.h"
#include "AuthoringReadNode.h"
#include "AuthoringWriteNode.h"
#include <type_traits>
#include <utility>
#include <array>
#include <string_view>

namespace TemporalProductSettingsIO
{
    // Missing block/fields migrate to native defaults. Malformed values and a
    // future schema fail closed rather than silently enabling optional SDKs.
    inline bool Read(const Authoring::ReadNode& node, TemporalProductSettings& output)
    {
        TemporalProductSettings settings = output;
        if (!node)
        {
            output = settings;
            return true;
        }
        if (!node.IsMap())
        {
            return false;
        }
        constexpr std::array<std::string_view, 14> fields{
            "schemaVersion", "enabled", "upscaler", "quality", "frameGenerator", "interpolatedFrameCount",
            "latencyMode", "fallbackAa", "spatialMode", "spatialRenderScale", "spatialSharpness",
            "digitalVibrance", "vibranceIntensity", "saturationBoost" };
        std::uint32_t seen = 0;
        for (const auto entry : node.Map())
        {
            const auto key = entry.key.AsStringChecked();
            bool recognized = false;
            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                if (key == fields[index])
                {
                    const auto bit = std::uint32_t{ 1 } << index;
                    if ((seen & bit) != 0 || !entry.value.IsScalar())
                    {
                        return false;
                    }
                    seen |= bit;
                    recognized = true;
                    break;
                }
            }
            if (!recognized)
            {
                return false;
            }
        }
        const auto field = [&](const char* name, auto& value)
        {
            if (const auto item = node[name])
            {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::string>)
                {
                    value = item.AsStringChecked();
                }
                else
                {
                    value = item.template As<Value>();
                }
            }
        };
        field("schemaVersion", settings.schemaVersion);
        field("enabled", settings.enabled);
        field("upscaler", settings.upscaler);
        field("quality", settings.quality);
        field("frameGenerator", settings.frameGenerator);
        field("interpolatedFrameCount", settings.interpolatedFrameCount);
        field("latencyMode", settings.latencyMode);
        field("fallbackAa", settings.fallbackAa);
        field("spatialMode", settings.spatialMode);
        field("spatialRenderScale", settings.spatialRenderScale);
        field("spatialSharpness", settings.spatialSharpness);
        field("digitalVibrance", settings.digitalVibrance);
        field("vibranceIntensity", settings.vibranceIntensity);
        field("saturationBoost", settings.saturationBoost);
        if (!ValidateTemporalProductSettings(settings))
        {
            return false;
        }
        output = std::move(settings);
        return true;
    }

    inline void Write(Authoring::WriteNode target, const TemporalProductSettings& settings)
    {
        Authoring::WriteDocument replacement;
        const auto node = replacement.Root();
        node.SetMap();
        node.Child("schemaVersion").SetScalar(settings.schemaVersion);
        node.Child("enabled").SetScalar(settings.enabled);
        node.Child("upscaler").SetScalar(settings.upscaler);
        node.Child("quality").SetScalar(settings.quality);
        node.Child("frameGenerator").SetScalar(settings.frameGenerator);
        node.Child("interpolatedFrameCount").SetScalar(settings.interpolatedFrameCount);
        node.Child("latencyMode").SetScalar(settings.latencyMode);
        node.Child("fallbackAa").SetScalar(settings.fallbackAa);
        node.Child("spatialMode").SetScalar(settings.spatialMode);
        node.Child("spatialRenderScale").SetScalar(settings.spatialRenderScale);
        node.Child("spatialSharpness").SetScalar(settings.spatialSharpness);
        node.Child("digitalVibrance").SetScalar(settings.digitalVibrance);
        node.Child("vibranceIntensity").SetScalar(settings.vibranceIntensity);
        node.Child("saturationBoost").SetScalar(settings.saturationBoost);
        // Replace only the owned portable block, dropping obsolete/foreign
        // fields while preserving every surrounding project setting.
        target.Assign(node);
    }
}
