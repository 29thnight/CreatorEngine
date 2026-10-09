#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace experiment::cooked
{
    enum class TextureColorSpace : std::uint8_t { Source, Linear, Srgb };
    enum class TextureCompression : std::uint8_t { None, Auto, BC1, BC3, BC5, BC7 };
    // GenerateFull appends a missing tail and retains every authored level.
    enum class TextureMipPolicy : std::uint8_t { PreserveAuthored, GenerateFull };
    enum class TextureCompressionQuality : std::uint8_t { Fast, Normal, High };

    struct TextureImportSettings final
    {
        TextureColorSpace colorSpace{ TextureColorSpace::Source };
        // None means no additional compression: authored DDS blocks are retained.
        TextureCompression compression{ TextureCompression::None };
        TextureMipPolicy mipPolicy{ TextureMipPolicy::PreserveAuthored };
        std::uint32_t maxDimension{};
        bool normalMap{};
        bool preserveAlphaCoverage{};
        float alphaCutoff{ 0.5f };
        TextureCompressionQuality compressionQuality{ TextureCompressionQuality::Normal };
        bool operator==(const TextureImportSettings&) const = default;
    };

    inline constexpr std::uint32_t kTextureImporterVersion = 2u;

    // Overlays known fields from the existing importSettings map. Missing fields
    // retain caller defaults (e.g. a model's material role). Failure is transactional.
    // Sampler wrap/filter and unrelated importer fields are not image settings.
    [[nodiscard]] bool ParseTextureImportSettings(std::string_view metadataYaml,
        TextureImportSettings& inOutSettings, std::string& failure);
    [[nodiscard]] bool ValidateTextureImportSettings(const TextureImportSettings& settings, std::string& failure);
    // Offline recipe/fingerprint functions are defined in the codec TU; parser
    // and validation remain linkable without the source-image SDK.
    [[nodiscard]] std::string TextureImportRecipe(const TextureImportSettings& settings);
    [[nodiscard]] std::string TextureCookerFingerprint();
}
