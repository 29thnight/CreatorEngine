#include "CookedEnvironment.h"
#include "../RHI/DX12/EnhancedIBLGenerator.h"
#include "../Experiment/Cooked/CookedAssetManifest.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <vector>
#include <Windows.h>

namespace assets
{
namespace
{
constexpr char kEnvironmentMagic[8]{'C','E','I','B','L','0','0','1'};
constexpr uint64_t kEnvironmentMaxBytes = 256ull * 1024 * 1024;
std::string EnvironmentHex(const Hash::Sha256Digest& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (uint8_t byte : value) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
bool EnvironmentError(std::string& error, std::string message)
{
    error = std::move(message); return false;
}
bool EnvironmentSizes(uint32_t cube, uint32_t brdf)
{
    return cube >= 32 && cube <= 1024 && (cube & (cube - 1)) == 0 && brdf >= 16 &&
           brdf <= 1024 && (brdf & (brdf - 1)) == 0;
}
std::array<TextureImage, 4> EnvironmentImages(uint32_t cube, uint32_t brdf)
{
    return {TextureImage::Allocate(RHIFormat::RGBA16Float, cube, cube, 6,
                EnhancedIBLGenerator::CubeMipCount(cube), true),
            TextureImage::Allocate(RHIFormat::RGBA16Float, std::min(cube,64u), std::min(cube,64u), 6, 1, true),
            TextureImage::Allocate(RHIFormat::RGBA16Float, cube, cube, 6, EnhancedIBLGenerator::kPrefilterMips, true),
            TextureImage::Allocate(RHIFormat::RGBA16Float, brdf, brdf, 1, 1)};
}
bool EnvironmentCollectShader(const std::filesystem::path& file, const std::filesystem::path& root,
    std::set<std::filesystem::path>& files, std::string& error)
{
    const auto normalized = file.lexically_normal();
    if (!files.insert(normalized).second) return true;
    std::ifstream input(normalized);
    if (!input) return EnvironmentError(error, "Environment shader missing: " + normalized.string());
    std::string line;
    while (std::getline(input, line))
    {
        const auto include = line.find("#include");
        if (include == std::string::npos) continue;
        const auto begin = line.find('"', include), end = begin == std::string::npos ? begin : line.find('"', begin+1);
        if (end == std::string::npos) continue;
        const auto name = line.substr(begin+1, end-begin-1);
        auto target = normalized.parent_path() / name;
        if (!std::filesystem::is_regular_file(target)) target = root / "Includes" / name;
        if (!EnvironmentCollectShader(target, root, files, error)) return false;
    }
    return true;
}
}

bool EnvironmentSourceIdentity(const std::filesystem::path& source, Hash::Sha256Digest& result, std::string& error)
{
    std::ifstream input(source, std::ios::binary);
    if (!input) return EnvironmentError(error, "Environment source missing: " + source.string());
    Hash::Sha256 hash;
    std::array<char, 65536> chunk;
    while (input) { input.read(chunk.data(), chunk.size()); hash.Update(chunk.data(), size_t(input.gcount())); }
    if (!input.eof()) return EnvironmentError(error, "Environment source read failed");
    result = hash.Finish(); error.clear(); return true;
}

bool EnvironmentRecipeIdentity(const std::filesystem::path& shaders, uint32_t cubeSize,
    uint32_t brdfSize, Hash::Sha256Digest& result, std::string& error)
{
    if (!EnvironmentSizes(cubeSize, brdfSize)) return EnvironmentError(error, "Invalid environment cook sizes");
    constexpr const char* stages[]{"IblFace.slang", "IblFullscreen.slang", "IblRectToCube.slang",
        "IblCubeDownsample.slang", "IblIrradiance.slang", "IblPrefilter.slang", "IblBrdf.slang",
        "IblImportanceRows.slang", "IblImportanceMarginal.slang", "IblImportanceSamples.slang"};
    std::set<std::filesystem::path> files;
    for (const auto* stage : stages)
        if (!EnvironmentCollectShader(shaders / stage, shaders, files, error)) return false;
    Hash::Sha256 hash;
    constexpr char recipe[] = "ce.environment.v1;rgba16f;linear-rec709;E-over-pi;D3D-cube;MIS1024;prefilter6;envMips7";
    hash.Update(recipe, sizeof(recipe));
    hash.Update(&cubeSize, sizeof(cubeSize)); hash.Update(&brdfSize, sizeof(brdfSize));
    for (const auto& file : files)
    {
        const auto relative = file.lexically_relative(shaders).generic_string();
        Hash::Sha256Digest digest;
        if (!EnvironmentSourceIdentity(file, digest, error)) return false;
        hash.Update(relative.data(), relative.size()); hash.Update(digest.data(), digest.size());
    }
    result = hash.Finish(); error.clear(); return true;
}

std::string EnvironmentCacheName(const EnvironmentIdentity& identity)
{
    return EnvironmentHex(identity.source) + "-" + EnvironmentHex(identity.recipe) + ".ceibl";
}

bool ReadCookedEnvironment(const std::filesystem::path& file, CookedEnvironment& result,
    std::string& error, const EnvironmentIdentity* expected)
{
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return EnvironmentError(error, "Cooked environment missing: " + file.string());
    const auto size = input.tellg();
    if (size < 112 || uint64_t(size) > kEnvironmentMaxBytes) return EnvironmentError(error, "Invalid cooked environment size");
    input.seekg(0);
    std::array<char, 8> magic;
    Hash::Sha256Digest digest;
    input.read(magic.data(), magic.size()); input.read(reinterpret_cast<char*>(digest.data()), digest.size());
    std::vector<std::byte> body(size_t(size)-40);
    input.read(reinterpret_cast<char*>(body.data()), body.size());
    Hash::Sha256Digest actual;
    if (!input || std::memcmp(magic.data(), kEnvironmentMagic, 8) != 0 ||
        !experiment::cooked::ComputeSha256(body, actual, error) || actual != digest)
        return EnvironmentError(error, "Cooked environment signature/checksum differs");
    CookedEnvironment candidate;
    std::memcpy(&candidate.cubeSize, body.data(), 4); std::memcpy(&candidate.brdfSize, body.data()+4, 4);
    std::memcpy(candidate.identity.source.data(), body.data()+8, 32);
    std::memcpy(candidate.identity.recipe.data(), body.data()+40, 32);
    if (!EnvironmentSizes(candidate.cubeSize, candidate.brdfSize) ||
        (expected && candidate.identity != *expected))
        return EnvironmentError(error, "Cooked environment source/recipe/settings differ");
    candidate.images = EnvironmentImages(candidate.cubeSize, candidate.brdfSize);
    size_t required = 72;
    for (const auto& image : candidate.images) required += image.TotalBytes();
    if (body.size() != required) return EnvironmentError(error, "Cooked environment payload layout differs");
    size_t offset = 72;
    for (auto& image : candidate.images)
        for (uint32_t face=0; face<image.ArraySize(); ++face)
            for (uint32_t mip=0; mip<image.MipLevels(); ++mip)
            {
                const auto& slice = *image.Find(mip,face);
                std::memcpy(image.MutablePixelsAt(slice), body.data()+offset, slice.slicePitch);
                offset += slice.slicePitch;
            }
    result = std::move(candidate); error.clear(); return true;
}

bool WriteCookedEnvironment(const std::filesystem::path& file, const CookedEnvironment& value, std::string& error)
{
    if (!EnvironmentSizes(value.cubeSize, value.brdfSize)) return EnvironmentError(error, "Invalid environment cook settings");
    std::vector<std::byte> body(72);
    size_t bytes = 72;
    for (const auto& image : value.images) bytes += image.TotalBytes();
    if (bytes > kEnvironmentMaxBytes - 40) return EnvironmentError(error,"Environment cook payload is too large");
    body.reserve(bytes);
    std::memcpy(body.data(), &value.cubeSize, 4); std::memcpy(body.data()+4, &value.brdfSize, 4);
    std::memcpy(body.data()+8, value.identity.source.data(),32); std::memcpy(body.data()+40, value.identity.recipe.data(),32);
    for (size_t index=0; index<4; ++index)
    {
        const auto& image = value.images[index];
        const uint32_t size = index==1 ? std::min(value.cubeSize,64u) : index==3 ? value.brdfSize : value.cubeSize;
        const uint32_t mips = index==0 ? EnhancedIBLGenerator::CubeMipCount(size) : index==2 ? EnhancedIBLGenerator::kPrefilterMips : 1u;
        if (!image.IsValid() || image.Format()!=RHIFormat::RGBA16Float || image.Width()!=size ||
            image.Height()!=size || image.ArraySize()!=(index<3 ? 6u : 1u) || image.MipLevels()!=mips ||
            image.IsCube()!=(index<3)) return EnvironmentError(error,"Environment cook image layout differs");
        for (uint32_t face=0; face<image.ArraySize(); ++face)
            for (uint32_t mip=0; mip<image.MipLevels(); ++mip)
            {
                const auto& slice = *image.Find(mip,face);
                body.insert(body.end(), slice.pixels, slice.pixels+slice.slicePitch);
            }
    }
    Hash::Sha256Digest digest;
    if (!experiment::cooked::ComputeSha256(body,digest,error)) return false;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(),ec);
    if (ec) return EnvironmentError(error,"Cannot create environment cache directory: "+ec.message());
    auto temporary = file; temporary += ".tmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetCurrentThreadId());
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    output.write(kEnvironmentMagic,8); output.write(reinterpret_cast<const char*>(digest.data()),digest.size());
    output.write(reinterpret_cast<const char*>(body.data()),body.size()); output.close();
    if (!output || !MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary,ec);
        return EnvironmentError(error,"Cannot publish cooked environment atomically");
    }
    error.clear(); return true;
}
}
