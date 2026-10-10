#pragma once

#include "../AssetIdentity.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    inline constexpr std::uint32_t kAssetManifestMagic = 0x464d4543u; // CEMF
    inline constexpr std::uint16_t kAssetManifestVersion = 2u;

    // GPU-ready CECT texture payload. CEMF itself remains unchanged.
    inline constexpr std::uint32_t kTextureArtifactVersion = 2u;

    // standalone material artifact 의 버전. ShaderMeta 처럼 유도할 schema 상수가
    // 없다 — `Material::reflect()` 는 버전을 들지 않는다. 그래서 여기 손으로
    // 둔다. 저작 스키마가 바뀌면 **이 숫자를 함께 올려야 하고**, 안 올리면
    // 구형 artifact 가 새 리더에 조용히 들어간다.
    inline constexpr std::uint32_t kMaterialArtifactVersion = 2u;
    inline constexpr std::uint32_t kMaterialProgramArtifactVersion = 5u;

    // scene/prefab artifact 의 버전. 저작 스키마가 바뀌면 함께 올려야 한다.
    inline constexpr std::uint32_t kSceneArtifactVersion = 2u;

    enum class CookedAssetKind : std::uint8_t
    {
        Model = 1,
        Material = 2,
        Texture = 3,
        ShaderMeta = 4,
        Scene = 5,
        Prefab = 6,
        AudioClip = 7,
        MaterialProgram = 8,
        CollisionGeometry = 9,
        SoundGraph = 10,
        SoundPreset = 11,
        // CEMF v3 only. The legacy v2 validator deliberately rejects these.
        Mesh = 12,
        Skeleton = 13,
        AnimationClip = 14,
        InputGraph = 15,
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

    // D5 Player cutover identity table. Cooked entries describe artifacts; this
    // table describes every packaged source asset that previously required a
    // `.meta` sidecar scan. Paths are normalized UTF-8 paths relative to the
    // package Assets root (for example `Models/Probe.glb`).
    struct AssetSourceManifestEntry final
    {
        AssetId assetId{};
        std::string sourcePath{};
    };

    struct CookedAssetManifest final
    {
        std::vector<CookedAssetManifestEntry> entries{};
        std::vector<AssetSourceManifestEntry> sourceAssets{};

        [[nodiscard]] const CookedAssetManifestEntry* Find(
            const AssetId& assetId) const noexcept;
        [[nodiscard]] const AssetSourceManifestEntry* FindSource(
            const AssetId& assetId) const noexcept;
    };

    // CEMF v3 is a separately validated AssetSet contract. There is no automatic
    // conversion from the untyped dependency list in a legacy CEMF v2 package.
    inline constexpr std::uint16_t kAssetSetManifestVersion = 3u;
    inline constexpr std::size_t kAssetSetManifestMaxBytes = 512u * 1024u * 1024u;
    inline constexpr std::uint32_t kAssetSetManifestMaxEntries = 1024u * 1024u;
    inline constexpr std::uint32_t kAssetSetManifestMaxDependencies = 4u * 1024u * 1024u;
    inline constexpr std::uint32_t kAssetSetManifestMaxStringBytes = 64u * 1024u * 1024u;

    struct AssetIdentity final
    {
        AssetId assetId{};
        // Nil selects the whole asset. Otherwise this is a stable UUIDv4/v8,
        // never a transient model array index or a runtime generation handle.
        AssetId subassetId{};

        friend auto operator<=>(const AssetIdentity&, const AssetIdentity&) noexcept = default;
    };

    struct TypedAssetReference final
    {
        AssetIdentity key{};
        CookedAssetKind kind{ CookedAssetKind::Model };

        friend auto operator<=>(const TypedAssetReference&, const TypedAssetReference&) noexcept = default;
    };

    enum class AssetDependencyKind : std::uint8_t
    {
        Hard = 1,
        Loadable = 2,
    };

    enum class AssetDependencyScope : std::uint8_t
    {
        Internal = 1,
        External = 2,
    };

    struct AssetDependency final
    {
        TypedAssetReference target{};
        AssetDependencyKind kind{ AssetDependencyKind::Hard };
        AssetDependencyScope scope{ AssetDependencyScope::Internal };

        friend auto operator<=>(const AssetDependency&, const AssetDependency&) noexcept = default;
    };

    // Content address includes type, representation, schema and target ABI.
    // Matching SHA-256 alone does not permit typed payload/decode sharing.
    // artifactPath locates these exact bytes in the associated mount backing.
    struct AssetBlobRecord final
    {
        Sha256Digest contentSha256{};
        std::uint64_t byteSize{};
        CookedAssetKind kind{ CookedAssetKind::Model };
        std::uint32_t representation{};
        std::uint32_t schemaVersion{};
        std::string targetPlatform{};
        std::string targetAbi{};
        std::string artifactPath{};

        friend auto operator<=>(const AssetBlobRecord&, const AssetBlobRecord&) = default;
    };

    struct AssetSetEntry final
    {
        TypedAssetReference asset{};
        std::uint32_t blobIndex{};
        std::vector<AssetDependency> dependencies{};
    };

    struct AssetSetManifest final
    {
        AssetId assetSetId{};
        std::uint64_t revision{};
        std::string targetPlatform{};
        std::string targetAbi{};
        std::vector<AssetSetEntry> entries{};
        std::vector<TypedAssetReference> roots{};
        std::vector<AssetBlobRecord> blobs{};

        // Valid for authored and normalized manifests; performs no payload I/O.
        [[nodiscard]] const AssetSetEntry* Find(
            const AssetIdentity& identity) const noexcept;
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
    //   New texture artifacts use .cetex; the path helper also validates legacy
    //   caller spelling without deriving logical identity from an extension.
    [[nodiscard]] std::string MakeDerivedTextureArtifactPath(
        const AssetId& textureAssetId, std::string_view extension);

    [[nodiscard]] std::string MakeDerivedShaderMetaArtifactPath(
        const AssetId& shaderMetaAssetId);

    // ★ standalone material 전용이다. 모델에 딸린 material 은 model CEMC 안의
    //   subasset 이라 model artifact 경로를 그대로 쓴다 — 여기서 경로를 만들면
    //   존재하지 않는 파일을 가리키게 된다.
    [[nodiscard]] std::string MakeDerivedMaterialArtifactPath(
        const AssetId& materialAssetId);

    [[nodiscard]] std::string MakeDerivedMaterialProgramArtifactPath(
        const AssetId& graphAssetId);

    [[nodiscard]] std::string MakeDerivedSceneArtifactPath(
        const AssetId& sceneAssetId);

    [[nodiscard]] std::string MakeDerivedPrefabArtifactPath(
        const AssetId& prefabAssetId);

    [[nodiscard]] std::string MakeDerivedAudioClipArtifactPath(
        const AssetId& audioClipAssetId);

    inline constexpr std::uint32_t kSoundAssetArtifactVersion = 1u;
    [[nodiscard]] std::string MakeDerivedSoundGraphArtifactPath(const AssetId& assetId);
    [[nodiscard]] std::string MakeDerivedSoundPresetArtifactPath(const AssetId& assetId);

    [[nodiscard]] bool ComputeSha256(std::span<const std::byte> bytes,
        Sha256Digest& outDigest, std::string& outError) noexcept;

    // 같은 논리 manifest는 입력 순서와 무관하게 같은 bytes를 낸다. cooked entry,
    // source identity와 dependency를 UUID 순서로 정규화한 뒤 CEMF v2 binary를 기록한다.
    [[nodiscard]] AssetManifestWriteResult WriteAssetManifest(
        const CookedAssetManifest& manifest);

    // 실패 시 outManifest는 바꾸지 않는다.
    [[nodiscard]] bool ReadAssetManifest(std::span<const std::byte> bytes,
        CookedAssetManifest& outManifest,
        std::vector<AssetManifestIssue>& outIssues);

    // Validates local type/reference integrity and rejects hard ownership cycles.
    // Explicit external hard edges must additionally resolve, with the expected
    // type, in the complete captured mount set before a catalog is published.
    // Only external loadable edges may remain unresolved until RequestAsync.
    [[nodiscard]] bool ValidateAssetSetManifest(
        const AssetSetManifest& manifest,
        std::vector<AssetManifestIssue>& outIssues);

    // Sorts entries, roots, edges and blob records, remapping blob indices.
    // Both functions are transactional: failed input never replaces the output.
    [[nodiscard]] bool NormalizeAssetSetManifest(
        const AssetSetManifest& manifest, AssetSetManifest& outManifest,
        std::vector<AssetManifestIssue>& outIssues);

    [[nodiscard]] AssetManifestWriteResult WriteAssetSetManifest(
        const AssetSetManifest& manifest);

    [[nodiscard]] bool ReadAssetSetManifest(std::span<const std::byte> bytes,
        AssetSetManifest& outManifest,
        std::vector<AssetManifestIssue>& outIssues);

    [[nodiscard]] bool VerifyArtifact(
        const CookedAssetManifestEntry& entry,
        std::uint64_t actualByteSize,
        const Sha256Digest& actualSha256,
        std::vector<AssetManifestIssue>& outIssues);
}
