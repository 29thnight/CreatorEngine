#pragma once

#include "../AssetIdentity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    inline constexpr std::uint32_t kAssetManifestMagic = 0x464d4543u; // CEMF
    inline constexpr std::uint16_t kAssetManifestVersion = 1u;

    // pass-through texture artifact 의 버전이다. 트랜스코딩(BC7·밉)이
    // 들어오는 날 2 가 되고 구버전 artifact 는 자동으로 거부된다.
    inline constexpr std::uint32_t kTextureArtifactVersion = 1u;

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

    // ★ 경로를 만드는 지점을 헤더 하나로 모은다. SerializationPlan §3.6.1 이
    //   이걸 명시적으로 요구한다 — legacy 는 쓰기·읽기가 Models 폴더 고정인데
    //   사용 판정만 원본 옆을 보는 바람에, Assets/Models 밖의 모델은 쿠킹이
    //   있어도 매번 Assimp 를 돌았다. 경로를 만드는 지점이 갈라지면 반드시
    //   이렇게 어긋난다.
    //
    //   extension 은 소문자 점 포함 표기다(".png"). pass-through artifact 라
    //   확장자가 곧 포맷이고, 로더가 그것으로 디코더를 고른다.
    [[nodiscard]] std::string MakeDerivedTextureArtifactPath(
        const AssetId& textureAssetId, std::string_view extension);

    [[nodiscard]] std::string MakeDerivedShaderMetaArtifactPath(
        const AssetId& shaderMetaAssetId);

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
