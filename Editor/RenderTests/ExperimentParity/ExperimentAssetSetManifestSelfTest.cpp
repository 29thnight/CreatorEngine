#include "Experiment/Cooked/CookedAssetManifest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ck = experiment::cooked;

        struct AssetSetChecker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const char* message)
            {
                if (condition)
                {
                    ++passed;
                }
                else
                {
                    ++failed;
                    log += "    [failed] ";
                    log += message;
                    log += '\n';
                }
            }
        };

        [[nodiscard]] experiment::AssetId SetTestId(std::uint8_t value)
        {
            experiment::AssetId id;
            id.value.data[6] = 0x40u;
            id.value.data[8] = 0x80u;
            id.value.data[15] = value;
            return id;
        }

        [[nodiscard]] ck::AssetSetManifest MakeSetManifest()
        {
            ck::AssetSetManifest manifest;
            manifest.assetSetId = SetTestId(1u);
            manifest.revision = 1u;
            manifest.targetPlatform = "win-x64";
            manifest.targetAbi = "creator-test-v1";
            const auto addBlob = [&](ck::CookedAssetKind kind, std::uint8_t hash)
            {
                ck::AssetBlobRecord blob;
                blob.kind = kind;
                blob.contentSha256[0] = hash;
                blob.byteSize = 16u;
                blob.representation = 1u;
                blob.schemaVersion = 1u;
                blob.targetPlatform = manifest.targetPlatform;
                blob.targetAbi = manifest.targetAbi;
                blob.artifactPath = "Derived/AssetBlobs/" + std::to_string(hash) + ".bin";
                manifest.blobs.push_back(std::move(blob));
            };
            addBlob(ck::CookedAssetKind::Mesh, 30u);
            addBlob(ck::CookedAssetKind::Skeleton, 10u);
            addBlob(ck::CookedAssetKind::Texture, 40u);
            addBlob(ck::CookedAssetKind::AnimationClip, 20u);

            const ck::TypedAssetReference mesh{ { SetTestId(2u), SetTestId(11u) }, ck::CookedAssetKind::Mesh };
            const ck::TypedAssetReference skeleton{ { SetTestId(2u), SetTestId(12u) }, ck::CookedAssetKind::Skeleton };
            const ck::TypedAssetReference texture{ { SetTestId(3u), {} }, ck::CookedAssetKind::Texture };
            const ck::TypedAssetReference clip{ { SetTestId(2u), SetTestId(13u) }, ck::CookedAssetKind::AnimationClip };
            manifest.entries = {
                { mesh, 0u, {
                    { skeleton, ck::AssetDependencyKind::Hard, ck::AssetDependencyScope::Internal },
                    { texture, ck::AssetDependencyKind::Loadable, ck::AssetDependencyScope::Internal } } },
                { skeleton, 1u, {} },
                { texture, 2u, {} },
                { clip, 3u, {
                    { skeleton, ck::AssetDependencyKind::Hard, ck::AssetDependencyScope::Internal } } },
                // Distinct identities deliberately share one compatible blob.
                { { { SetTestId(4u), {} }, ck::CookedAssetKind::Texture }, 2u, {} },
            };
            manifest.roots = { mesh, clip };
            return manifest;
        }

        void SetU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
        {
            for (unsigned index = 0u; index < 4u; ++index)
            {
                bytes[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xffu);
            }
        }
    }

    // Synthetic regression source only. No source assets, payload I/O or engine
    // initialization are required. Register this with the RenderTests runner.
    [[nodiscard]] bool RunExperimentAssetSetManifestSelfTest(std::string& outLog)
    {
        AssetSetChecker check{ outLog };
        const ck::AssetSetManifest manifest = MakeSetManifest();
        const auto encoded = ck::WriteAssetSetManifest(manifest);
        check.Check(encoded.Succeeded(), "typed granular AssetSet writes");
        if (!encoded.Succeeded())
        {
            return false;
        }
        ck::AssetSetManifest decoded;
        std::vector<ck::AssetManifestIssue> issues;
        check.Check(ck::ReadAssetSetManifest(encoded.bytes, decoded, issues), "v3 round trip reads");
        for (const auto& entry : manifest.entries)
        {
            const auto* found = decoded.Find(entry.asset.key);
            check.Check(found != nullptr && found->asset.kind == entry.asset.kind,
                "stable asset/subasset identity and expected type preserved");
            if (found != nullptr && found->blobIndex < decoded.blobs.size())
            {
                check.Check(decoded.blobs[found->blobIndex] == manifest.blobs[entry.blobIndex],
                    "canonical blob permutation preserves exact identity binding");
            }
        }
        check.Check(decoded.blobs.size() == 4u && decoded.entries.size() == 5u,
            "compatible identities share one blob table record");

        auto reordered = manifest;
        std::ranges::reverse(reordered.entries);
        std::ranges::reverse(reordered.roots);
        std::ranges::reverse(reordered.blobs);
        for (auto& entry : reordered.entries)
        {
            entry.blobIndex = static_cast<std::uint32_t>(reordered.blobs.size() - 1u - entry.blobIndex);
            std::ranges::reverse(entry.dependencies);
        }
        const auto reorderedBytes = ck::WriteAssetSetManifest(reordered);
        check.Check(reorderedBytes.Succeeded() && reorderedBytes.bytes == encoded.bytes,
            "entry/root/edge/blob input order cannot change canonical bytes");

        const auto rejected = [&](const ck::AssetSetManifest& candidate)
        {
            return !ck::WriteAssetSetManifest(candidate).Succeeded();
        };
        auto invalid = manifest;
        invalid.entries[1].dependencies.push_back({ invalid.entries[0].asset,
            ck::AssetDependencyKind::Hard, ck::AssetDependencyScope::Internal });
        const auto cyclic = ck::WriteAssetSetManifest(invalid);
        check.Check(!cyclic.Succeeded() && !cyclic.issues.empty()
            && cyclic.issues.back().message.find(" -> ") != std::string::npos,
            "hard ownership cycle rejected with a readable identity path");
        invalid = manifest;
        invalid.entries[1].dependencies.push_back({ invalid.entries[1].asset,
            ck::AssetDependencyKind::Hard, ck::AssetDependencyScope::Internal });
        check.Check(rejected(invalid), "self-hard cycle rejected");
        invalid.entries[1].dependencies[0].kind = ck::AssetDependencyKind::Loadable;
        check.Check(!rejected(invalid), "self-loadable reference remains legal");
        invalid.entries[1].dependencies[0].target = invalid.entries[0].asset;
        check.Check(!rejected(invalid), "mixed hard/loadable cycle remains legal");

        invalid = manifest;
        invalid.entries[0].dependencies[0].target.key.assetId = SetTestId(99u);
        check.Check(rejected(invalid), "missing internal hard target rejected");
        invalid.entries[0].dependencies[0].kind = ck::AssetDependencyKind::Loadable;
        check.Check(rejected(invalid), "missing internal loadable target requires external declaration");
        invalid.entries[0].dependencies[0].scope = ck::AssetDependencyScope::External;
        check.Check(!rejected(invalid), "explicit external loadable target may remain unresolved");
        invalid.entries[0].dependencies[0].kind = ck::AssetDependencyKind::Hard;
        check.Check(!rejected(invalid), "external hard target deferred to union mount validation");
        invalid.entries[0].dependencies[0].target.kind = static_cast<ck::CookedAssetKind>(255u);
        check.Check(rejected(invalid), "external target still requires a valid expected type");
        invalid = manifest;
        invalid.entries[0].dependencies[0].scope = ck::AssetDependencyScope::External;
        check.Check(rejected(invalid), "external declaration cannot ambiguously resolve locally");

        invalid = manifest;
        invalid.entries[0].dependencies[0].target.kind = ck::CookedAssetKind::Texture;
        check.Check(rejected(invalid), "dependency type mismatch rejected");
        invalid = manifest;
        invalid.roots[0].kind = ck::CookedAssetKind::Texture;
        check.Check(rejected(invalid), "root type mismatch rejected");
        invalid = manifest;
        invalid.roots[0].key.assetId = SetTestId(99u);
        check.Check(rejected(invalid), "missing typed root rejected");
        invalid = manifest;
        invalid.entries[0].blobIndex = 2u;
        check.Check(rejected(invalid), "entry versus blob type mismatch rejected");
        invalid = manifest;
        invalid.entries[0].blobIndex = 99u;
        check.Check(rejected(invalid), "out of range blob index rejected");
        invalid = manifest;
        invalid.entries[4].asset.key = invalid.entries[0].asset.key;
        check.Check(rejected(invalid), "full identity is unique even across different types");
        invalid = manifest;
        invalid.entries[0].dependencies.push_back(invalid.entries[0].dependencies[0]);
        invalid.entries[0].dependencies.back().kind = ck::AssetDependencyKind::Loadable;
        check.Check(rejected(invalid), "contradictory duplicate edges rejected");
        invalid = manifest;
        invalid.entries[0].asset.key.subassetId.value.data[6] = 0x50u;
        check.Check(rejected(invalid), "unsupported stable subasset UUID version rejected");
        invalid = manifest;
        invalid.blobs.push_back(invalid.blobs[0]);
        check.Check(rejected(invalid), "duplicate compatible content address rejected");
        invalid = manifest;
        invalid.blobs[0].targetPlatform = "other-platform";
        check.Check(rejected(invalid), "platform incompatibility rejected");
        invalid = manifest;
        invalid.blobs[0].targetAbi = "other-abi";
        check.Check(rejected(invalid), "ABI incompatibility rejected");
        invalid = manifest;
        invalid.blobs[0].schemaVersion = 0u;
        check.Check(rejected(invalid), "empty blob schema rejected");
        invalid = manifest;
        invalid.blobs[0].representation = 0u;
        check.Check(rejected(invalid), "empty blob representation rejected");
        invalid = manifest;
        invalid.blobs[0].artifactPath = "Derived/../escape.bin";
        check.Check(rejected(invalid), "path traversal rejected");
        invalid.blobs[0].artifactPath = std::string("Derived/bad\0path.bin", 20u);
        check.Check(rejected(invalid), "embedded NUL path rejected");
        invalid.blobs[0].artifactPath = "Derived/\xc0\xaf.bin";
        check.Check(rejected(invalid), "overlong UTF-8 path rejected");

        // Every failed reader must preserve the previous, valid output.
        const auto rejectBytes = [&](const std::vector<std::byte>& bytes)
        {
            auto output = manifest;
            output.revision = 987u;
            std::vector<ck::AssetManifestIssue> errors;
            const bool read = ck::ReadAssetSetManifest(bytes, output, errors);
            return !read && !errors.empty() && output.revision == 987u
                && output.entries.size() == manifest.entries.size();
        };
        bool everyTruncationRejected = true;
        for (std::size_t size = 0u; size < encoded.bytes.size(); ++size)
        {
            const std::vector<std::byte> truncated(encoded.bytes.begin(), encoded.bytes.begin() + size);
            everyTruncationRejected = rejectBytes(truncated) && everyTruncationRejected;
        }
        check.Check(everyTruncationRejected, "all truncation boundaries fail transactionally");
        auto corrupt = encoded.bytes;
        SetU32(corrupt, 32u, 0xffffffffu);
        check.Check(rejectBytes(corrupt), "unbounded entry count rejected before allocation");
        corrupt = encoded.bytes;
        SetU32(corrupt, 40u, 0xffffffffu);
        check.Check(rejectBytes(corrupt), "unbounded dependency count rejected before allocation");
        corrupt = encoded.bytes;
        SetU32(corrupt, 52u, 1u);
        check.Check(rejectBytes(corrupt), "overlapping or noncanonical string offsets rejected");
        corrupt = encoded.bytes;
        SetU32(corrupt, 68u, 1u);
        check.Check(rejectBytes(corrupt), "nonzero header reserved field rejected");
        corrupt = encoded.bytes;
        corrupt.push_back(std::byte{ 0u });
        check.Check(rejectBytes(corrupt), "trailing data rejected");
        corrupt = encoded.bytes;
        corrupt[4] = std::byte{ 4u };
        check.Check(rejectBytes(corrupt), "unknown future CEMF version rejected");

        ck::CookedAssetManifest legacy;
        ck::CookedAssetManifestEntry legacyEntry;
        legacyEntry.assetId = SetTestId(90u);
        legacyEntry.kind = ck::CookedAssetKind::Texture;
        legacyEntry.formatVersion = ck::kTextureArtifactVersion;
        legacyEntry.byteSize = 16u;
        legacyEntry.contentSha256[0] = 1u;
        legacyEntry.artifactPath = "Derived/Textures/legacy.png";
        legacy.entries.push_back(legacyEntry);
        const auto legacyBytes = ck::WriteAssetManifest(legacy);
        ck::CookedAssetManifest legacyParsed;
        issues.clear();
        check.Check(legacyBytes.Succeeded()
            && ck::ReadAssetManifest(legacyBytes.bytes, legacyParsed, issues),
            "legacy v2 writer and reader remain functional");
        check.Check(rejectBytes(legacyBytes.bytes), "v2 cannot become an AssetSet without source recook");
        issues.clear();
        check.Check(!ck::ReadAssetManifest(encoded.bytes, legacyParsed, issues),
            "legacy reader cannot accept v3 AssetSet bytes");
        legacy.entries[0].kind = ck::CookedAssetKind::Mesh;
        check.Check(!ck::WriteAssetManifest(legacy).Succeeded(),
            "legacy v2 known-kind acceptance is not broadened");

        auto normalizeOutput = manifest;
        normalizeOutput.revision = 654u;
        issues.clear();
        invalid = manifest;
        invalid.entries.clear();
        check.Check(!ck::NormalizeAssetSetManifest(invalid, normalizeOutput, issues)
            && normalizeOutput.revision == 654u, "normalization failure preserves previous output");
        outLog += "  AssetSet manifest: " + std::to_string(check.passed) + " passed, "
            + std::to_string(check.failed) + " failed\n";
        return check.failed == 0u;
    }
}
