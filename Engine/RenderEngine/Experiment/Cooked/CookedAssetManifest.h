#pragma once

#include "../AssetIdentity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace experiment::cooked
{
    inline constexpr std::uint32_t kAssetManifestMagic = 0x464d4543u; // CEMF
    inline constexpr std::uint16_t kAssetManifestVersion = 1u;

    enum class CookedAssetKind : std::uint8_t
    {
        Model = 1,
        Material = 2,
        Texture = 3,
        ShaderMeta = 4,
        Scene = 5,
        Prefab = 6,
    };

    using Sha256Digest = std::array<std::uint8_t, 32>;

    struct CookedAssetManifestEntry final
    {
        AssetId assetId{};
        CookedAssetKind kind{ CookedAssetKind::Model };
        std::uint32_t formatVersion{};
        std::uint64_t byteSize{};
        Sha256Digest contentSha256{};

        // pak 안의 normalized UTF-8 virtual path. 절대/역슬래시/dot segment는
        // 허용하지 않고 모든 cooked artifact는 Derived/ 아래에 둔다.
        std::string artifactPath{};
        std::vector<AssetId> dependencies{};
    };

    struct CookedAssetManifest final
    {
        std::vector<CookedAssetManifestEntry> entries{};

        [[nodiscard]] const CookedAssetManifestEntry* Find(
            const AssetId& assetId) const noexcept;
    };

    struct AssetManifestIssue final
    {
        std::string context{};
        std::string message{};
    };

    struct AssetManifestWriteResult final
    {
        std::vector<std::byte> bytes{};
        std::vector<AssetManifestIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return issues.empty() && !bytes.empty();
        }
    };

    [[nodiscard]] std::string MakeDerivedModelArtifactPath(
        const AssetId& modelAssetId);

    [[nodiscard]] bool ComputeSha256(std::span<const std::byte> bytes,
        Sha256Digest& outDigest, std::string& outError) noexcept;

    // 같은 논리 manifest는 입력 순서와 무관하게 같은 bytes를 낸다. entry와
    // dependency를 UUID 순서로 정규화한 뒤 CEMF v1 binary를 기록한다.
    [[nodiscard]] AssetManifestWriteResult WriteAssetManifest(
        const CookedAssetManifest& manifest);

    // 실패 시 outManifest는 바꾸지 않는다.
    [[nodiscard]] bool ReadAssetManifest(std::span<const std::byte> bytes,
        CookedAssetManifest& outManifest,
        std::vector<AssetManifestIssue>& outIssues);

    [[nodiscard]] bool VerifyArtifact(
        const CookedAssetManifestEntry& entry,
        std::uint64_t actualByteSize,
        const Sha256Digest& actualSha256,
        std::vector<AssetManifestIssue>& outIssues);
}
