#pragma once
#include "../TextureImage.h"
#include "Sha256.h"
#include <array>
#include <filesystem>
#include <string>

namespace assets
{
struct EnvironmentIdentity
{
    Hash::Sha256Digest source{}, recipe{};
    bool operator==(const EnvironmentIdentity&) const = default;
};
struct CookedEnvironment
{
    EnvironmentIdentity identity;
    uint32_t cubeSize{}, brdfSize{};
    // Cube with mips, E/pi irradiance, GGX prefilter, BRDF split sum.
    std::array<TextureImage, 4> images;
};
bool EnvironmentRecipeIdentity(const std::filesystem::path& shaders, uint32_t cubeSize,
    uint32_t brdfSize, Hash::Sha256Digest& result, std::string& error);
bool EnvironmentSourceIdentity(const std::filesystem::path& source,
    Hash::Sha256Digest& result, std::string& error);
std::string EnvironmentCacheName(const EnvironmentIdentity& identity);
bool ReadCookedEnvironment(const std::filesystem::path& file, CookedEnvironment& result, std::string& error,
    const EnvironmentIdentity* expected = nullptr);
bool WriteCookedEnvironment(const std::filesystem::path& file, const CookedEnvironment& value, std::string& error);
}
