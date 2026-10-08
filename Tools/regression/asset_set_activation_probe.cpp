// Unrun source fixture for the RenderEngine-linked metadata activation boundary.
// It performs no shader compilation, texture decode or GPU submission.
#include "../../Engine/RenderEngine/AssetDepot/AssetSetActivation.h"
#include "../../Engine/Utility_Framework/ContentAbi.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace cooked = experiment::cooked;

    void Require(bool value, const char* failure)
    {
        if (!value)
        {
            throw std::runtime_error(failure);
        }
    }

    std::string Hash(std::span<const std::byte> bytes)
    {
        cooked::Sha256Digest digest{};
        std::string failure;
        Require(cooked::ComputeSha256(bytes, digest, failure), "Hash failed");
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (const auto value : digest)
        {
            result.push_back(digits[value >> 4u]);
            result.push_back(digits[value & 15u]);
        }
        return result;
    }

    void Write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Require(static_cast<bool>(file), "Fixture write failed");
    }

    void Text(const std::filesystem::path& path, const std::string& text)
    {
        Write(path, { reinterpret_cast<const std::byte*>(text.data()), text.size() });
    }

    experiment::AssetId Id(unsigned char value)
    {
        experiment::AssetId id;
        id.value.data[6] = 0x40u;
        id.value.data[8] = 0x80u;
        id.value.data[15] = value;
        return id;
    }

    void VerifyActivation(const std::filesystem::path& root)
    {
        std::string failure;
        std::vector<cooked::AssetSetMountInput> inputs;
        Require(AssetDepot::ReadConfiguredAssetSets(root, inputs, failure) && inputs.empty(),
            "Legacy absent activation policy failed");
        const std::vector<std::byte> payload{ std::byte{ 0x12 } };
        const auto payloadHash = Hash(payload);
        cooked::AssetSetManifest manifest;
        manifest.assetSetId = Id(1u);
        manifest.revision = 1u;
        manifest.targetPlatform = "win-x64";
        manifest.targetAbi = CreatorContentAbi::Token;
        cooked::AssetBlobRecord blob;
        Require(cooked::ComputeSha256(payload, blob.contentSha256, failure), "Blob hash failed");
        blob.byteSize = payload.size();
        blob.kind = cooked::CookedAssetKind::Texture;
        blob.representation = 1u;
        blob.schemaVersion = 1u;
        blob.targetPlatform = manifest.targetPlatform;
        blob.targetAbi = manifest.targetAbi;
        blob.artifactPath = "Derived/AssetBlobs/" + std::string(64u, '0') + "/" + payloadHash + ".png";
        manifest.blobs.push_back(blob);
        const cooked::TypedAssetReference asset{ { Id(2u), {} }, cooked::CookedAssetKind::Texture };
        manifest.entries.push_back({ asset, 0u, {} });
        manifest.roots.push_back(asset);
        std::vector<cooked::AssetManifestIssue> issues;
        Require(AssetDepot::ValidateAssetSetRuntimeCompatibility(manifest, issues),
            "Supported Texture schema was rejected before mount");
        auto incompatible = manifest;
        ++incompatible.blobs.front().schemaVersion;
        Require(!AssetDepot::ValidateAssetSetRuntimeCompatibility(incompatible, issues) && !issues.empty(),
            "Unknown decoder schema passed mount preflight");
        incompatible = manifest;
        incompatible.blobs.front().kind = cooked::CookedAssetKind::Scene;
        Require(!AssetDepot::ValidateAssetSetRuntimeCompatibility(incompatible, issues),
            "Future unimplemented asset kind passed mount preflight");
        const auto encoded = cooked::WriteAssetSetManifest(manifest);
        Require(encoded.Succeeded(), "Fixture manifest failed");
        const auto manifestHash = Hash(encoded.bytes);
        const auto setRoot = root / "AssetSets" / manifestHash;
        Write(setRoot / "Derived/asset-set-manifest.cemf", encoded.bytes);
        Write(setRoot / blob.artifactPath, payload);
        const auto policy = root / "Derived/asset-set-activation.ceas";
        const auto good = std::string("CEAS1\nwin-x64\n") + CreatorContentAbi::Token + "\n" + manifestHash + "\n";
        Text(policy, good);
        Require(AssetDepot::ReadConfiguredAssetSets(root, inputs, failure) && inputs.size() == 1u
            && inputs.front().manifest.assetSetId == manifest.assetSetId, "Configured set was not captured");
        Require(AssetDepot::ValidateConfiguredAssetSets(root, failure), "Valid activation metadata failed");
        Text(policy, "CEAS1\nwin-x64\nwrong-host-abi\n" + manifestHash + "\n");
        Require(!AssetDepot::ValidateConfiguredAssetSets(root, failure), "Host ABI mismatch was accepted");
        // Both policy and manifest claiming the same wrong ABI must still fail.
        auto selfAsserted = manifest;
        selfAsserted.targetAbi = "wrong-host-abi";
        selfAsserted.blobs.front().targetAbi = selfAsserted.targetAbi;
        const auto wrongBytes = cooked::WriteAssetSetManifest(selfAsserted);
        Require(wrongBytes.Succeeded(), "Wrong-ABI fixture could not be serialized");
        const auto wrongHash = Hash(wrongBytes.bytes);
        Write(root / "AssetSets" / wrongHash / "Derived/asset-set-manifest.cemf", wrongBytes.bytes);
        Text(policy, "CEAS1\nwin-x64\nwrong-host-abi\n" + wrongHash + "\n");
        Require(!AssetDepot::ReadConfiguredAssetSets(root, inputs, failure), "Self-asserted host content ABI accepted");
        Text(policy, std::string("CEAS1\nwin-x64\n") + CreatorContentAbi::Token + "\n../outside\n");
        Require(!AssetDepot::ReadConfiguredAssetSets(root, inputs, failure) && inputs.size() == 1u,
            "Traversal was accepted or failure destroyed the prior parse result");
        Text(policy, good + manifestHash + "\n");
        Require(!AssetDepot::ReadConfiguredAssetSets(root, inputs, failure), "Duplicate set was accepted");
        Text(policy, good);
        auto damaged = encoded.bytes;
        damaged.back() ^= std::byte{ 1 };
        Write(setRoot / "Derived/asset-set-manifest.cemf", damaged);
        Require(!AssetDepot::ReadConfiguredAssetSets(root, inputs, failure) && inputs.size() == 1u,
            "Manifest hash mismatch was accepted or replaced prior inputs");
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("asset-set-activation-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        std::filesystem::create_directories(root);
        VerifyActivation(root);
        std::filesystem::remove_all(root);
        std::cout << "ASSET_SET_ACTIVATION_OK\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
