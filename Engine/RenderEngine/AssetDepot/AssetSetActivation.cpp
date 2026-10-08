#include "../DataSystem.h"
#include "AssetSetActivation.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

#include <algorithm>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    namespace cooked = experiment::cooked;
    constexpr std::size_t ActivationMaxBytes = 16384u;
    constexpr std::size_t ActivationManifestBudget = 128u * 1024u * 1024u;

    std::vector<std::byte> ReadActivationArtifact(
        own::shared_owner<const cooked::ArtifactByteSource> source,
        std::string_view path, std::size_t limit)
    {
        std::string failure;
        std::uint64_t size{};
        if (!cooked::CaptureArtifactSource(source, path, failure)
            || !source->Size(path, size, failure) || size == 0u || size > limit)
        {
            throw std::runtime_error("AssetSet activation input is unavailable or oversized: " + failure);
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        if (!source->ReadAt(path, 0u, bytes, failure))
        {
            throw std::runtime_error("AssetSet activation read failed: " + failure);
        }
        return bytes;
    }

    std::string ActivationDigestText(const cooked::Sha256Digest& hash)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(64u);
        for (const auto value : hash)
        {
            result.push_back(digits[value >> 4u]);
            result.push_back(digits[value & 15u]);
        }
        return result;
    }

    std::string ActivationHash(std::span<const std::byte> bytes)
    {
        cooked::Sha256Digest hash{};
        std::string failure;
        if (!cooked::ComputeSha256(bytes, hash, failure))
        {
            throw std::runtime_error("AssetSet activation hash failed: " + failure);
        }
        return ActivationDigestText(hash);
    }
}

namespace AssetDepot
{
    bool ReadConfiguredAssetSets(const std::filesystem::path& assetRoot,
        std::vector<cooked::AssetSetMountInput>& outInputs, std::string& failure)
    {
        failure.clear();
        try
        {
            constexpr std::string_view activationPath = "Derived/asset-set-activation.ceas";
            std::error_code error;
            if (!file::exists(assetRoot / activationPath, error))
            {
                if (error)
                {
                    throw std::runtime_error("Cannot inspect AssetSet activation list: " + error.message());
                }
                outInputs.clear();
                return true; // Existing packages do not opt into automatic v3 activation.
            }
            const auto bytes = ReadActivationArtifact(
                own::make_shared<const cooked::LooseArtifactByteSource>(assetRoot), activationPath, ActivationMaxBytes);
            std::istringstream lines(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            const auto line = [&lines]()
            {
                std::string value;
                if (!std::getline(lines, value) || value.find('\r') != std::string::npos
                    || value.find('\0') != std::string::npos)
                {
                    throw std::runtime_error("Malformed AssetSet activation list.");
                }
                return value;
            };
            if (line() != "CEAS1" || line() != "win-x64")
            {
                throw std::runtime_error("Unsupported AssetSet activation format/platform.");
            }
            const auto abi = line();
            if (abi.empty() || abi.size() > 128u || !std::ranges::all_of(abi, [](unsigned char value)
                { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                    || (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.'; }))
            {
                throw std::runtime_error("AssetSet activation requires an explicit host ABI token.");
            }
            std::vector<cooked::AssetSetMountInput> inputs;
            std::set<std::string> seen;
            std::size_t remaining = ActivationManifestBudget;
            while (lines.peek() != std::char_traits<char>::eof())
            {
                const auto hash = line();
                if (inputs.size() == 64u || hash.size() != 64u || !seen.insert(hash).second
                    || !std::ranges::all_of(hash, [](unsigned char value)
                        { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
                {
                    throw std::runtime_error("AssetSet activation needs 1..64 distinct manifest SHA256 values.");
                }
                cooked::AssetSetMountInput input;
                input.byteSource = own::make_shared<const cooked::LooseArtifactByteSource>(
                    assetRoot / "AssetSets" / hash);
                const auto manifestBytes = ReadActivationArtifact(input.byteSource,
                    "Derived/asset-set-manifest.cemf", remaining);
                remaining -= manifestBytes.size();
                std::vector<cooked::AssetManifestIssue> issues;
                if (ActivationHash(manifestBytes) != hash
                    || !cooked::ReadAssetSetManifest(manifestBytes, input.manifest, issues))
                {
                    throw std::runtime_error("AssetSet activation manifest hash/schema mismatch: " + hash);
                }
                input.options.expectedTargetPlatform = "win-x64";
                input.options.expectedTargetAbi = abi;
                inputs.push_back(std::move(input));
            }
            if (inputs.empty())
            {
                throw std::runtime_error("AssetSet activation list is empty.");
            }

            outInputs = std::move(inputs);
            return true;
        }
        catch (const std::exception& exception)
        {
            failure = exception.what();
            return false;
        }
    }

    bool ValidateConfiguredAssetSets(const std::filesystem::path& assetRoot, std::string& failure)
    {
        std::vector<cooked::AssetSetMountInput> inputs;
        if (!ReadConfiguredAssetSets(assetRoot, inputs, failure))
        {
            return false;
        }
        if (inputs.empty())
        {
            return true;
        }
        std::uint64_t mount{};
        for (auto& input : inputs)
        {
            input.mountId.value = ++mount;
            // The package writer separately hashes every CAS filename. Bind
            // those verified filenames/sizes to the native CEMF declarations;
            // runtime activation itself still reads only manifest metadata.
            for (const auto& blob : input.manifest.blobs)
            {
                std::uint64_t size{};
                if (std::filesystem::path(blob.artifactPath).stem().string()
                        != ActivationDigestText(blob.contentSha256)
                    || !input.byteSource->Size(blob.artifactPath, size, failure)
                    || size != blob.byteSize)
                {
                    failure = "AssetSet activation blob locator/size mismatch: " + blob.artifactPath;
                    return false;
                }
            }
        }
        const cooked::CookedAssetCatalog empty;
        cooked::CookedAssetCatalog candidate;
        std::vector<cooked::AssetManifestIssue> issues;
        if (!empty.WithMountedAssetSets(inputs, inputs.size(), candidate, issues))
        {
            failure = "AssetSet activation closure was rejected";
            for (const auto& issue : issues)
            {
                failure += " | " + issue.context + ": " + issue.message;
            }
            return false;
        }
        return true;
    }
}

bool DataSystem::MountConfiguredAssetSets(const file::path& assetRoot, std::string& failure)
{
    failure.clear();
    try
    {
        std::vector<cooked::AssetSetMountInput> inputs;
        if (!AssetDepot::ReadConfiguredAssetSets(assetRoot, inputs, failure))
        {
            return false;
        }
        if (inputs.empty())
        {
            return true;
        }
        own::shared_owner<const cooked::CookedAssetCatalog> snapshot;
        std::uint64_t revision{};
        std::uint64_t epoch{};
        {
            std::lock_guard admissionLock(m_assetPreparationMutex);
            std::lock_guard catalogLock(m_cookedCatalogMutex);
            if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u
                || inputs.size() > (std::numeric_limits<std::uint64_t>::max)() - m_assetDepotRevision
                || inputs.size() > (std::numeric_limits<std::uint64_t>::max)() - m_nextAssetMountId)
            {
                throw std::runtime_error("AssetSet activation admission is stopped or exhausted.");
            }
            snapshot = m_cookedCatalog;
            revision = m_assetDepotRevision;
            epoch = m_assetPreparationEpoch;
            for (auto& input : inputs)
            {
                input.mountId.value = m_nextAssetMountId++;
            }
        }
        cooked::CookedAssetCatalog empty;
        cooked::CookedAssetCatalog candidate;
        std::vector<cooked::AssetManifestIssue> issues;
        if (!(snapshot ? *snapshot : empty).WithMountedAssetSets(
            inputs, revision + inputs.size(), candidate, issues))
        {
            failure = "AssetSet activation closure was rejected";
            for (const auto& issue : issues)
            {
                failure += " | " + issue.context + ": " + issue.message;
            }
            return false;
        }
        auto published = own::make_shared<const cooked::CookedAssetCatalog>(std::move(candidate));
        own::shared_owner<const cooked::CookedAssetCatalog> retired;
        AssetDepot::TextureAssetRetiredEntries retiredTextures;
        AssetDepot::ModelAssetRetiredEntries retiredModels;
        AssetDepot::MaterialAssetRetiredEntries retiredMaterials;
        LegacyCacheRetirement retiredLegacy;
        {
            std::lock_guard admissionLock(m_assetPreparationMutex);
            std::lock_guard catalogLock(m_cookedCatalogMutex);
            if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u
                || m_assetPreparationEpoch != epoch || m_assetDepotRevision != revision)
            {
                throw std::runtime_error("AssetSet activation resolver changed before publication.");
            }
            StageLegacyCacheRetirementLocked(retiredLegacy);
            StageTextureAssetRetirementLocked(retiredTextures);
            StageModelAssetRetirementLocked(retiredModels);
            StageMaterialAssetRetirementLocked(retiredMaterials);
            retired = std::move(m_cookedCatalog);
            m_cookedCatalog = std::move(published);
            m_assetDepotRevision = revision + inputs.size();
            DetachLegacyCachesLocked(retiredLegacy);
            InvalidateTextureAssetsLocked(retiredTextures);
            InvalidateModelAssetsLocked(retiredModels);
            InvalidateMaterialAssetsLocked(retiredMaterials);
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        failure = exception.what();
        return false;
    }
}
