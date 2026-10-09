#include "TextureImportSettings.h"
#include "AuthoringParsedDocument.h"

#include <charconv>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <set>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        template<class T>
        bool TextureSettingsEnum(Authoring::ReadNode node, T& value,
            std::initializer_list<std::pair<std::string_view, T>> choices)
        {
            if (!node.IsScalar())
            {
                return false;
            }
            for (const auto& choice : choices)
            {
                if (node.Scalar() == choice.first)
                {
                    value = choice.second;
                    return true;
                }
            }
            return false;
        }

        template<class T>
        bool TextureSettingsNumber(Authoring::ReadNode node, T& value)
        {
            if (!node.IsScalar())
            {
                return false;
            }
            const auto text = node.Scalar();
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        }

        bool TextureSettingsBool(Authoring::ReadNode node, bool& value)
        {
            if (!node.IsScalar() || (node.Scalar() != "true" && node.Scalar() != "false"))
            {
                return false;
            }
            value = node.Scalar() == "true";
            return true;
        }
    }

    bool ValidateTextureImportSettings(const TextureImportSettings& settings, std::string& failure)
    {
        failure.clear();
        if (settings.colorSpace > TextureColorSpace::Srgb || settings.compression > TextureCompression::BC7
            || settings.mipPolicy > TextureMipPolicy::GenerateFull
            || settings.compressionQuality > TextureCompressionQuality::High)
        {
            failure = "Texture import settings contain an unknown enum value.";
        }
        else if (settings.maxDimension > 16384u)
        {
            failure = "Texture maxDimension must be 0 (source size) or at most 16384.";
        }
        else if (!std::isfinite(settings.alphaCutoff) || settings.alphaCutoff < 0.0f || settings.alphaCutoff > 1.0f
            || (settings.preserveAlphaCoverage && (settings.alphaCutoff <= 0.0f || settings.alphaCutoff >= 1.0f)))
        {
            failure = "Texture alphaCutoff must be finite in [0, 1], or strictly inside (0, 1) for alpha coverage.";
        }
        else if (settings.normalMap && settings.colorSpace == TextureColorSpace::Srgb)
        {
            failure = "Normal maps require linear sampling, not Srgb.";
        }
        else if (settings.compression == TextureCompression::BC5 && !settings.normalMap)
        {
            failure = "BC5 requires normalMap=true and the two-channel normal consumer contract.";
        }
        else if (settings.normalMap && settings.preserveAlphaCoverage)
        {
            failure = "Alpha coverage is not supported for normal maps.";
        }
        return failure.empty();
    }

    bool ParseTextureImportSettings(std::string_view metadataYaml,
        TextureImportSettings& inOutSettings, std::string& failure)
    {
        failure.clear();
        if (metadataYaml.empty())
        {
            return ValidateTextureImportSettings(inOutSettings, failure);
        }
        try
        {
            const auto document = Authoring::ParsedDocument::ParseText(std::string(metadataYaml), failure);
            if (!document || !document.Root().IsMap())
            {
                failure = "Texture metadata must be a YAML mapping. " + failure;
                return false;
            }
            std::size_t settingsMaps = 0u;
            for (const auto entry : document.Root().Map())
            {
                if (entry.key.Scalar() == "importSettings")
                {
                    ++settingsMaps;
                }
            }
            if (settingsMaps > 1u)
            {
                failure = "Texture metadata contains duplicate importSettings maps.";
                return false;
            }
            const auto node = document.Root()["importSettings"];
            if (!node)
            {
                return ValidateTextureImportSettings(inOutSettings, failure);
            }
            if (!node.IsMap())
            {
                failure = "Texture importSettings must be a YAML mapping.";
                return false;
            }
            auto parsed = inOutSettings;
            std::set<std::string_view> seen;
            for (const auto entry : node.Map())
            {
                const auto key = entry.key.Scalar();
                const auto value = entry.value;
                bool valid = true;
                if (key == "colorSpace")
                {
                    valid = TextureSettingsEnum(value, parsed.colorSpace, {
                        { "Source", TextureColorSpace::Source }, { "Linear", TextureColorSpace::Linear },
                        { "Srgb", TextureColorSpace::Srgb } });
                }
                else if (key == "compression")
                {
                    valid = TextureSettingsEnum(value, parsed.compression, {
                        { "None", TextureCompression::None }, { "Auto", TextureCompression::Auto },
                        { "BC1", TextureCompression::BC1 }, { "BC3", TextureCompression::BC3 },
                        { "BC5", TextureCompression::BC5 }, { "BC7", TextureCompression::BC7 } });
                }
                else if (key == "mipPolicy")
                {
                    valid = TextureSettingsEnum(value, parsed.mipPolicy, {
                        { "PreserveAuthored", TextureMipPolicy::PreserveAuthored },
                        { "GenerateFull", TextureMipPolicy::GenerateFull } });
                }
                else if (key == "compressionQuality")
                {
                    valid = TextureSettingsEnum(value, parsed.compressionQuality, {
                        { "Fast", TextureCompressionQuality::Fast }, { "Normal", TextureCompressionQuality::Normal },
                        { "High", TextureCompressionQuality::High } });
                }
                else if (key == "maxDimension")
                {
                    valid = TextureSettingsNumber(value, parsed.maxDimension);
                }
                else if (key == "alphaCutoff")
                {
                    valid = TextureSettingsNumber(value, parsed.alphaCutoff);
                }
                else if (key == "normalMap")
                {
                    valid = TextureSettingsBool(value, parsed.normalMap);
                }
                else if (key == "preserveAlphaCoverage")
                {
                    valid = TextureSettingsBool(value, parsed.preserveAlphaCoverage);
                }
                else
                {
                    continue;
                }
                if (!valid || !seen.insert(key).second)
                {
                    failure = "Invalid or duplicate texture import setting: " + std::string(key);
                    return false;
                }
            }
            if (!ValidateTextureImportSettings(parsed, failure))
            {
                return false;
            }
            inOutSettings = parsed;
            return true;
        }
        catch (const std::exception& error)
        {
            failure = "Cannot parse texture import settings: " + std::string(error.what());
            return false;
        }
    }

}
