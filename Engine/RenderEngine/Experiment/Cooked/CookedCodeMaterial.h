#pragma once

#include "CookedAssetManifest.h"
#include "../ModelData.h"
#include "../../ShaderMetaReflection.h"
#include "../../RHI/RHIShaderCompiler.h"

namespace experiment::cooked
{
    inline constexpr std::uint32_t kAuthoredMaterialRepresentation = 2u;
    inline constexpr std::uint32_t kAuthoredMaterialVersion = 1u;
    inline constexpr std::uint32_t kCodeProgramRepresentation = 2u;
    inline constexpr std::uint32_t kCodeProgramVersion = 1u;
    inline constexpr std::string_view kCodeProgramFormatStamp = "creator-code-verified-v1";
    inline constexpr std::string_view kCodeProgramAbiStamp = "creator-code-material-abi-v1";
    inline constexpr std::size_t kCodeProgramMaxBytes = 160u * 1024u * 1024u;
    inline constexpr std::size_t kCodeProgramMaxVariants = 4096u;

    struct CodeProgramInput final
    {
        // Canonical, Assets-relative path; actual captured bytes never ship.
        std::string path;
        std::uint64_t byteSize{};
        Sha256Digest sha256{};
        bool operator==(const CodeProgramInput&) const = default;
    };

    struct CodeProgramStage final
    {
        RHIShaderStage stage{};
        std::string entry;
        std::string profile;
        RHIShaderBlob bytecode;
        RHIShaderReflection reflection;
    };

    struct CodeProgramVariant final
    {
        RHIShaderBinary backend{};
        std::uint32_t passIndex{};
        std::vector<std::uint16_t> keywordSelections;
        std::uint32_t vertexAttributeMask{};
        bool referencePath{};
        RHIShaderPermutationKey materialPermutationKey{};
        RHIShaderPermutation permutation;
        std::vector<CodeProgramStage> stages;
    };

    // Value-only linked-program product. No source, device, compiler, manager,
    // runtime owner or adopting raw pointer is carried by this representation.
    struct CodeProgram final
    {
        AssetId programAssetId{};
        AssetId shaderMetaAssetId{};
        std::string metadataPath;
        std::string rootSourcePath;
        std::string compilerStamp; // Lowercase SHA-256 of the verified compiler/toolchain.
        std::string formatStamp{ kCodeProgramFormatStamp };
        std::string abiStamp{ kCodeProgramAbiStamp };
        std::vector<CodeProgramInput> inputs;
        std::vector<std::byte> metadataBytes;
        ShaderMeta meta;
        ShaderMetaBindingLayout layout;
        std::vector<CodeProgramVariant> variants;
    };

    struct AuthoredMaterialDocument final
    {
        Material material;
        AssetId programAssetId{};
        std::vector<AssetId> defaultTextureAssetIds;
    };

    [[nodiscard]] std::size_t CodeProgramRetainedBytes(const CodeProgram& program) noexcept;

    [[nodiscard]] bool EncodeAuthoredMaterialArtifact(const AuthoredMaterialDocument& document,
        std::vector<std::byte>& bytes, std::vector<AssetDependency>& dependencies, std::string& failure);
    [[nodiscard]] bool ReadAuthoredMaterialArtifact(std::span<const std::byte> bytes,
        const TypedAssetReference& expected, std::span<const AssetDependency> dependencies,
        AuthoredMaterialDocument& result, std::string& failure);
    [[nodiscard]] bool EncodeCodeProgramArtifact(const CodeProgram& program,
        std::vector<std::byte>& bytes, std::vector<AssetDependency>& dependencies, std::string& failure);
    [[nodiscard]] bool ReadCodeProgramArtifact(std::span<const std::byte> bytes,
        const TypedAssetReference& expected, std::span<const AssetDependency> dependencies,
        CodeProgram& result, std::string& failure);
    // The explicit verified bundle is the same bounded wire representation;
    // derive its dependency values before checking against a catalog entry.
    [[nodiscard]] bool ReadVerifiedCodeProgram(std::span<const std::byte> bytes,
        CodeProgram& result, std::vector<AssetDependency>& dependencies, std::string& failure);
    [[nodiscard]] bool CollectCodeProgramDefaultTextures(const ShaderMeta& meta,
        std::vector<AssetId>& result, std::string& failure);
    [[nodiscard]] bool ValidateAuthoredMaterialBinding(const AuthoredMaterialDocument& document,
        const CodeProgram& program, std::string& failure);
}
