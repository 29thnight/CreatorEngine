#pragma once

#include "Texture.h"
#include "AssetDepot/TextureAssetRuntime.h"

#include <optional>

// Neutral storage shared by the strict cooked reader and the authoring adapter.
// The Editor-only derived object retains SDK pixels in their original allocation.
// Player constructs only this base with owned cooked/procedural TextureImage data.
struct Texture::CodecImage
{
    TextureImage owned;
    std::vector<TextureSubimage> subresources;
    RHIFormat format{ RHIFormat::Unknown };
    uint32_t width{};
    uint32_t height{};
    uint32_t mipLevels{};
    uint32_t arraySize{};
    bool isCube{};
    bool hasAlpha{};
    bool colorSpaceLocked{};
    std::optional<AssetDepot::TextureImageKey> key{};
    std::size_t accountedBytes{};
    std::size_t sourceRetainedBytes{};

    void Account();
    virtual ~CodecImage();
};
