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
    // Scene BRDF MIS proposal: mip-0 luminance CDF rows, marginal, cached
    // direction/PDF samples. Persisted since CEIBL002. CEIBL003 stores source
    // and prefiltered radiance as float32; irradiance/BRDF remain float16.
    std::array<TextureImage, 3> importance;
    // CEIBL005 appends a 4096-sample reflection bank after the original 1024.
    bool importancePersisted{};
    // CEIBL004 retains the decoded linear source for exact sharp reflection.
    TextureImage source;
};
bool BuildEnvironmentImportance(CookedEnvironment& value, std::string& error);
bool EnvironmentRecipeIdentity(const std::filesystem::path& shaders, uint32_t cubeSize,
    uint32_t brdfSize, Hash::Sha256Digest& result, std::string& error);
bool EnvironmentSourceIdentity(const std::filesystem::path& source,
    Hash::Sha256Digest& result, std::string& error);
std::string EnvironmentCacheName(const EnvironmentIdentity& identity);
bool ReadCookedEnvironment(const std::filesystem::path& file, CookedEnvironment& result, std::string& error,
    const EnvironmentIdentity* expected = nullptr);
bool WriteCookedEnvironment(const std::filesystem::path& file, const CookedEnvironment& value, std::string& error);
}
