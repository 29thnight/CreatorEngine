#pragma once

#include "ArtifactByteSource.h"
#include "CookedAssetManifest.h"
#include "../../AssetDepot/AssetMountId.h"
#include "../../../Utility_Framework/Ownership.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace material_graph
{
    struct Budget;
    struct CookedProgram;
}

namespace experiment::cooked
{
    class CookedAudioClipSource;
    enum class AssetLookupStatus : std::uint8_t
    {
        Found,
        NotMounted,
        TypeMismatch,
        HardDependencyCycle,
    };

    struct AssetCatalogLookupIssue final
    {
        AssetLookupStatus status{ AssetLookupStatus::Found };
        TypedAssetReference asset{};
        // Root through the failing edge, including the repeated target on a cycle.
        std::vector<TypedAssetReference> dependencyPath{};
        std::string message{};
    };

    struct ResolvedAssetEntry final
    {
        AssetDepot::AssetMountId mountId{};
        AssetId assetSetId{};
        std::uint64_t manifestRevision{};
        std::uint64_t resolverRevision{};
        AssetSetEntry entry{};
        AssetBlobRecord blob{};
        // Exact locator metadata above and this source travel together. No raw
        // pointer borrows an entry or a source from a movable catalog vector.
        own::shared_owner<const ArtifactByteSource> byteSource{};
    };

    struct AssetSetMountOptions final
    {
        // Supplied by the host, never copied from an untrusted manifest as an
        // implicit compatibility decision. Both must be nonempty and exact.
        std::string expectedTargetPlatform{};
        std::string expectedTargetAbi{};
        // The default rejects every conflicting definition. An override names
        // each exact identity intentionally replaced by this transaction.
        std::vector<AssetIdentity> overrideIdentities{};
    };

    struct AssetSetMountInput final
    {
        AssetSetManifest manifest{};
        own::shared_owner<const ArtifactByteSource> byteSource{};
        AssetDepot::AssetMountId mountId{};
        AssetSetMountOptions options{};
    };

    // CEMF 하나를 읽어 **GUID 로 묻는** 런타임 경계.
    //
    // ★ 이것이 대체하려는 것은 `DataSystem::LoadAssetCatalog` 다. 지금은 부팅
    //   때 asset root 를 재귀 순회하며 모든 `.meta` 를 YAML 로 파싱해 GUID↔경로
    //   표를 만든다. pak 에 CEMF 가 실려 있으므로 그 스캔은 필요 없다 —
    //   바이너리 하나를 읽으면 같은 표가 나오고, **덤으로 의존까지 나온다.**
    //
    // ★ 모델·재질 소비는 별도 로더에 둔다. AU2 AudioClip은 여기서 GUID를
    //   찾아 byte source의 CEAC/CEMF 무결성까지 확인한 뒤 bounded payload
    //   reader를 돌려준다. 실제 오디오 backend 재생은 상위 계층의 책임이다.
    //
    // ★ CEMF v2는 두 표를 명시적으로 분리해 함께 싣는다. cooked entries는
    //   GUID→artifact, sourceAssets는 GUID→package source path다. Player는 후자로
    //   AssetMetaRegistry를 한 번 구성하고 `.meta`를 스캔하지 않는다. Editor는
    //   watcher가 갱신하는 source registry를 계속 정본으로 쓴다.
    class CookedAssetCatalog final
    {
    public:
        CookedAssetCatalog() = default;

        // CEMF 바이트에서 만든다. 실패하면 빈 catalog 를 돌려주고 issues 를
        // 채운다 — **부분적으로 채워진 catalog 를 내놓지 않는다.** 절반만 아는
        // 표는 "없는 자산"과 "아직 안 읽은 자산"을 구별하지 못한다.
        //
        // derivedRoot 는 `Derived/` 의 **부모**다(pak 마운트 루트 또는
        // 스테이징 Assets 디렉터리). artifactPath 가 `Derived/...` 로 시작하므로
        // 그대로 이어 붙이면 실제 파일이 된다.
        [[nodiscard]] static CookedAssetCatalog Load(
            std::span<const std::byte> manifestBytes,
            std::filesystem::path derivedRoot,
            std::vector<AssetManifestIssue>& outIssues);

        [[nodiscard]] bool IsEmpty() const noexcept { return entries_.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
        [[nodiscard]] std::size_t SourceAssetCount() const noexcept
        {
            return manifest_.sourceAssets.size();
        }
        [[nodiscard]] const std::filesystem::path& DerivedRoot() const noexcept
        {
            return derivedRoot_;
        }

        [[nodiscard]] const CookedAssetManifestEntry* Find(
            const AssetId& assetId) const noexcept;

        // artifact 의 실제 파일 경로. 모르는 GUID 면 빈 경로다.
        [[nodiscard]] std::filesystem::path ResolveArtifactPath(
            const AssetId& assetId) const;

        // Package Assets root 아래의 source 경로. D5 cutover 동안 아직 source를
        // 읽는 consumer와 AssetMetaRegistry를 `.meta` 스캔 없이 연결한다.
        [[nodiscard]] std::filesystem::path ResolveSourcePath(
            const AssetId& assetId) const;

        // GUID lookup plus CEAC/CEMF integrity verification. The byte source
        // may address the loose cooked tree or a mounted pak.
        [[nodiscard]] bool OpenAudioClip(const AssetId& assetId,
            std::shared_ptr<const ArtifactByteSource> bytes,
            CookedAudioClipSource& out, std::string& failure) const;

        [[nodiscard]] bool ReadCollisionGeometry(const AssetId& assetId, const ArtifactByteSource& bytes,
            std::vector<std::byte>& out, std::string& failure) const;

        [[nodiscard]] bool OpenMaterialProgram(const AssetId& assetId,
            const ArtifactByteSource& bytes, const material_graph::Budget& budget,
            material_graph::CookedProgram& out, std::string& failure) const;
        [[nodiscard]] std::span<const AssetSourceManifestEntry> SourceAssets()
            const noexcept
        {
            return manifest_.sourceAssets;
        }

        // I7-C2 — 신선도 판정은 전 entry를 한 번 훑어야 한다(마운트 때 한 번).
        // 표는 불변이라 span으로 내주는 것이 안전하다 — 수명은 catalog가 진다.
        [[nodiscard]] std::span<const CookedAssetManifestEntry> Entries()
            const noexcept
        {
            return entries_;
        }

        // 이 종류의 entry 만 센다. 진단·게이트용.
        [[nodiscard]] std::size_t CountOfKind(CookedAssetKind kind) const noexcept;

        // ★ root 를 로드하는 데 필요한 전부(자기 포함)를 **위상 순서**로 담는다.
        //   의존이 먼저 오고 root 가 마지막이다 — 로더가 그 순서대로 열면
        //   참조가 항상 이미 준비돼 있다.
        //
        //   순환은 방문 집합으로 자연히 멈춘다(중첩 프리팹이 서로를 품는 저작
        //   오류가 데이터로 존재할 수 있다). 해소되지 않는 의존만 실패다 —
        //   그건 manifest 가 깨졌다는 뜻이고 조용히 넘어가면 런타임에서 빈
        //   자산이 된다.
        [[nodiscard]] bool CollectClosure(const AssetId& root,
            std::vector<AssetId>& outOrdered, std::string& outFailure) const;

        // v3 mount-set operations are immutable candidate builders. Failure
        // leaves outCatalog unchanged; the caller atomically publishes success.
        // These operations and root listing perform no payload reads/decodes.
        // Legacy Load/Find/Entries retain their CEMF v2 meaning above.
        [[nodiscard]] bool WithMountedAssetSet(const AssetSetManifest& manifest,
            own::shared_owner<const ArtifactByteSource> byteSource,
            AssetDepot::AssetMountId mountId, std::uint64_t nextResolverRevision,
            const AssetSetMountOptions& options, CookedAssetCatalog& outCatalog,
            std::vector<AssetManifestIssue>& outIssues) const;

        // Bootstrap publishes one complete resolver. Cross-set hard edges are
        // validated only after every candidate is present; no partial snapshot
        // escapes. Individual manifests and duplicate/override rules still apply.
        [[nodiscard]] bool WithMountedAssetSets(std::span<const AssetSetMountInput> inputs,
            std::uint64_t nextResolverRevision, CookedAssetCatalog& outCatalog,
            std::vector<AssetManifestIssue>& outIssues) const;

        // Logical removal always advances the resolver. It may leave another
        // mount's external hard edge unresolved: its next acquisition reports
        // NotMounted. Old snapshots/results retain their exact source owners.
        [[nodiscard]] bool WithoutMountedAssetSet(AssetDepot::AssetMountId mountId,
            std::uint64_t nextResolverRevision, CookedAssetCatalog& outCatalog,
            std::vector<AssetManifestIssue>& outIssues) const;

        [[nodiscard]] std::uint64_t ResolverRevision() const noexcept { return m_resolverRevision; }
        [[nodiscard]] std::size_t MountCount() const noexcept { return m_mounts.size(); }
        [[nodiscard]] std::size_t MountedAssetCount() const noexcept { return m_mountedEntries.size(); }
        [[nodiscard]] bool IsMounted(AssetDepot::AssetMountId mountId) const noexcept;

        [[nodiscard]] AssetLookupStatus Find(const TypedAssetReference& asset,
            ResolvedAssetEntry& out) const;
        [[nodiscard]] std::vector<TypedAssetReference> ListRoots(
            AssetDepot::AssetMountId mountId, CookedAssetKind kind) const;

        // Dependency-first, root last, using only this resolver snapshot. Hard
        // cycles and missing/wrong-type targets fail with the full lookup path;
        // loadable edges do not contribute bulk or block this hard closure.
        // Failure clears outOrdered instead of exposing a partial generation.
        [[nodiscard]] AssetLookupStatus CollectHardClosure(const TypedAssetReference& root,
            std::vector<ResolvedAssetEntry>& outOrdered, AssetCatalogLookupIssue& outIssue) const;

    private:
        struct MountedAssetSet final
        {
            AssetDepot::AssetMountId mountId{};
            AssetSetManifest manifest{};
            own::shared_owner<const ArtifactByteSource> byteSource{};
            // Identical declarations inherit the selected definition's rank.
            // Only an explicitly named conflict receives a higher rank, so
            // removal/rebuild never invents a last-mounted-wins policy.
            std::vector<std::uint64_t> definitionRevisions{};
        };

        struct MountedEntryLocation final
        {
            std::size_t mountIndex{};
            std::size_t entryIndex{};
        };

        [[nodiscard]] bool AddMountedAssetSetCandidate(const AssetSetManifest& manifest,
            own::shared_owner<const ArtifactByteSource> byteSource,
            AssetDepot::AssetMountId mountId, std::uint64_t nextResolverRevision,
            const AssetSetMountOptions& options, bool validateHardGraph,
            CookedAssetCatalog& outCatalog, std::vector<AssetManifestIssue>& outIssues) const;
        void RebuildMountedIndex();
        [[nodiscard]] ResolvedAssetEntry ResolveMountedEntry(const MountedEntryLocation& location) const;
        [[nodiscard]] bool ValidateMountedHardGraph(AssetCatalogLookupIssue& outIssue) const;
        [[nodiscard]] AssetLookupStatus TraverseHardClosure(const TypedAssetReference& root,
            std::map<AssetIdentity, std::uint8_t>& states, std::vector<ResolvedAssetEntry>* outOrdered,
            AssetCatalogLookupIssue& outIssue) const;

        std::vector<MountedAssetSet> m_mounts{};
        std::map<AssetIdentity, MountedEntryLocation> m_mountedEntries{};
        std::uint64_t m_resolverRevision{};
        std::string m_targetPlatform{};
        std::string m_targetAbi{};

        CookedAssetManifest manifest_{};
        std::vector<CookedAssetManifestEntry> entries_{};
        std::filesystem::path derivedRoot_{};
    };
}
