#include "CookedAssetCatalog.h"
#include "CookedAudioClipSource.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace experiment::cooked
{
    CookedAssetCatalog CookedAssetCatalog::Load(
        std::span<const std::byte> manifestBytes,
        std::filesystem::path derivedRoot,
        std::vector<AssetManifestIssue>& outIssues)
    {
        CookedAssetCatalog catalog;

        CookedAssetManifest manifest;
        if (!ReadAssetManifest(manifestBytes, manifest, outIssues))
        {
            // ★ 빈 catalog 를 돌려준다. 부분적으로 채워 두면 "없는 자산"과
            //   "아직 안 읽은 자산"을 구별할 수 없다.
            return catalog;
        }
        if (manifest.entries.empty())
        {
            outIssues.push_back(AssetManifestIssue{ "catalog",
                "빈 manifest로는 catalog를 세우지 않는다." });
            return catalog;
        }
        if (derivedRoot.empty())
        {
            outIssues.push_back(AssetManifestIssue{ "catalog.derivedRoot",
                "derived root가 비어 있다." });
            return catalog;
        }

        catalog.entries_ = manifest.entries;
        catalog.manifest_ = std::move(manifest);
        catalog.derivedRoot_ = std::move(derivedRoot);
        return catalog;
    }

    const CookedAssetManifestEntry* CookedAssetCatalog::Find(
        const AssetId& assetId) const noexcept
    {
        return manifest_.Find(assetId);
    }

    std::filesystem::path CookedAssetCatalog::ResolveArtifactPath(
        const AssetId& assetId) const
    {
        const CookedAssetManifestEntry* entry = Find(assetId);
        if (!entry || entry->artifactPath.empty()) return {};
        // artifactPath 는 `Derived/...` 로 시작하는 normalized virtual path 다
        // (manifest 계약이 강제한다). 그대로 이어 붙인다.
        return derivedRoot_ / std::filesystem::path(entry->artifactPath);
    }

    std::filesystem::path CookedAssetCatalog::ResolveSourcePath(
        const AssetId& assetId) const
    {
        const AssetSourceManifestEntry* entry = manifest_.FindSource(assetId);
        if (!entry) return {};
        const auto* first = reinterpret_cast<const char8_t*>(entry->sourcePath.data());
        const std::u8string utf8Path(first, first + entry->sourcePath.size());
        return (derivedRoot_ / std::filesystem::path(utf8Path))
            .lexically_normal();
    }

    bool CookedAssetCatalog::ReadCollisionGeometry(const AssetId& assetId, const ArtifactByteSource& bytes,
        std::vector<std::byte>& out, std::string& failure) const
    {
        out.clear();
        const auto* entry = Find(assetId);
        if (!entry || entry->kind != CookedAssetKind::CollisionGeometry || entry->formatVersion != 1 ||
            !IsCollisionGeometryArtifactVirtualPath(entry->artifactPath))
        {
            failure = "Collision geometry artifact missing or incompatible";
            return false;
        }
        own::shared_owner<const ArtifactByteSource> exactBytes;
        if (!bytes.CaptureArtifact(entry->artifactPath, exactBytes, failure))
        {
            return false;
        }
        const auto& source = exactBytes ? *exactBytes : bytes;
        std::uint64_t size = 0;
        if (!source.Size(entry->artifactPath, size, failure)
            || size != entry->byteSize || size < 68 || size > 256 * 1024 * 1024)
        {
            failure = "Collision geometry artifact missing or incompatible";
            return false;
        }
        try
        {
            std::vector<std::byte> prepared(static_cast<std::size_t>(size));
            if (!source.ReadAt(entry->artifactPath, 0, prepared, failure))
            {
                return false;
            }

            Sha256Digest digest;
            if (!ComputeSha256(prepared, digest, failure) || digest != entry->contentSha256)
            {
                failure = "Collision geometry CEMF digest mismatch";
                return false;
            }
            out.swap(prepared);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            failure = "Collision geometry artifact allocation failed";
            return false;
        }
    }

    bool CookedAssetCatalog::OpenAudioClip(const AssetId& assetId,
        std::shared_ptr<const ArtifactByteSource> bytes,
        CookedAudioClipSource& out, std::string& failure) const
    {
        const CookedAssetManifestEntry* entry = Find(assetId);
        if (!entry)
        {
            failure = "audio clip GUID is absent from cooked manifest";
            return false;
        }
        return OpenCookedAudioClipEntry(*entry, std::move(bytes), out, failure);
    }

    std::size_t CookedAssetCatalog::CountOfKind(
        CookedAssetKind kind) const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count_if(entries_,
            [kind](const CookedAssetManifestEntry& entry)
            {
                return entry.kind == kind;
            }));
    }

    bool CookedAssetCatalog::CollectClosure(const AssetId& root,
        std::vector<AssetId>& outOrdered, std::string& outFailure) const
    {
        outOrdered.clear();
        outFailure.clear();

        if (!Find(root))
        {
            outFailure = "root GUID가 catalog에 없다: "
                + Uuid::ToString(root.value);
            return false;
        }

        // 재귀 대신 명시 스택. 자산 그래프 깊이는 얕지만, 깊이를 데이터가
        // 정하는 순회를 재귀로 두면 저작 실수 하나가 스택을 넘긴다.
        //
        // 후위 순회로 위상 순서를 만든다: 의존을 모두 낸 뒤에 자기를 낸다.
        enum class State : std::uint8_t { Enter, Emit };
        struct Frame final { AssetId id; State state; };

        std::set<AssetId> visited;
        std::set<AssetId> emitted;
        std::vector<Frame> stack;
        stack.push_back({ root, State::Enter });

        while (!stack.empty())
        {
            const Frame frame = stack.back();
            stack.pop_back();

            if (frame.state == State::Emit)
            {
                if (emitted.insert(frame.id).second)
                    outOrdered.push_back(frame.id);
                continue;
            }

            // ★ 순환은 여기서 자연히 멈춘다. 중첩 프리팹이 서로를 품는
            //   저작 오류가 데이터로 존재할 수 있고, 그때도 로드는 되어야 한다.
            if (!visited.insert(frame.id).second) continue;

            const CookedAssetManifestEntry* entry = Find(frame.id);
            if (!entry)
            {
                // ★ **여기는 도달할 수 없다.** 그런데도 두는 이유는 UB 때문이지
                //   방어 때문이 아니다 — 아래에서 `entry->dependencies` 를
                //   역참조하므로 null 검사 자체는 있어야 한다.
                //
                //   도달 불가인 근거: `ReadAssetManifest` 가 `ValidateManifest` 를
                //   부르고, 그것이 **모든 dependency 가 manifest entry 로
                //   해석되는지** 이미 검사한다. 즉 catalog 에는 폐포가 닫힌
                //   manifest 만 들어온다. root 는 위에서 따로 확인했으므로
                //   여기 오는 id 는 전부 누군가의 dependency 다.
                //
                //   변이로 확인했다: 이 분기를 `continue` 로 바꿔도 게이트가
                //   초록이다(태울 데이터가 없다). "혹시 모르니"로 남긴 것이
                //   아니라 도달 불가를 확인한 뒤 남긴 것이고, 그 사실을 여기
                //   적어 둔다.
                outFailure = "해소되지 않는 dependency: "
                    + Uuid::ToString(frame.id.value);
                outOrdered.clear();
                return false;
            }

            stack.push_back({ frame.id, State::Emit });
            // 역순으로 넣어 원래 dependency 순서대로 처리되게 한다.
            for (auto it = entry->dependencies.rbegin();
                it != entry->dependencies.rend(); ++it)
            {
                stack.push_back({ *it, State::Enter });
            }
        }

        return true;
    }

    namespace
    {
        [[nodiscard]] std::string MountedIdentityText(const AssetIdentity& identity)
        {
            std::string text = Uuid::ToString(identity.assetId.value);
            if (identity.subassetId.IsValid())
            {
                text += "/" + Uuid::ToString(identity.subassetId.value);
            }
            return text;
        }

        [[nodiscard]] bool SameMountedDefinition(const AssetSetManifest& leftManifest,
            const AssetSetEntry& left, const AssetSetManifest& rightManifest, const AssetSetEntry& right)
        {
            const AssetBlobRecord& leftBlob = leftManifest.blobs[left.blobIndex];
            const AssetBlobRecord& rightBlob = rightManifest.blobs[right.blobIndex];
            // Locator paths are mount-local, not part of an immutable content
            // definition. Every semantic/compatibility field must still match.
            return left.asset == right.asset && left.dependencies == right.dependencies
                && leftBlob.kind == rightBlob.kind && leftBlob.representation == rightBlob.representation
                && leftBlob.schemaVersion == rightBlob.schemaVersion && leftBlob.byteSize == rightBlob.byteSize
                && leftBlob.contentSha256 == rightBlob.contentSha256
                && leftBlob.targetPlatform == rightBlob.targetPlatform && leftBlob.targetAbi == rightBlob.targetAbi;
        }

        [[nodiscard]] std::string MountedSourceText(AssetDepot::AssetMountId mountId,
            const AssetSetManifest& manifest, const AssetSetEntry& entry)
        {
            return "mount " + std::to_string(mountId.value) + ", AssetSet "
                + Uuid::ToString(manifest.assetSetId.value) + " revision " + std::to_string(manifest.revision)
                + ", " + manifest.blobs[entry.blobIndex].artifactPath;
        }

        [[nodiscard]] bool MountIssue(std::vector<AssetManifestIssue>& outIssues,
            std::string context, std::string message)
        {
            outIssues.push_back({ std::move(context), std::move(message) });
            return false;
        }
    }

    bool CookedAssetCatalog::IsMounted(AssetDepot::AssetMountId mountId) const noexcept
    {
        return std::ranges::any_of(m_mounts, [mountId](const MountedAssetSet& mount)
        {
            return mount.mountId == mountId;
        });
    }

    bool CookedAssetCatalog::WithMountedAssetSet(const AssetSetManifest& manifest,
        own::shared_owner<const ArtifactByteSource> byteSource, AssetDepot::AssetMountId mountId,
        std::uint64_t nextResolverRevision, const AssetSetMountOptions& options,
        CookedAssetCatalog& outCatalog, std::vector<AssetManifestIssue>& outIssues) const
    {
        outIssues.clear();
        if (&outCatalog == this)
        {
            return MountIssue(outIssues, "catalog.mount", "The immutable candidate must not alias its input snapshot.");
        }
        if (!mountId.IsValid() || IsMounted(mountId))
        {
            return MountIssue(outIssues, "catalog.mountId", "Mount ID is invalid or already active.");
        }
        if (!byteSource)
        {
            return MountIssue(outIssues, "catalog.byteSource",
                "Mount requires an owned immutable artifact byte source.");
        }
        if (nextResolverRevision <= m_resolverRevision)
        {
            return MountIssue(outIssues, "catalog.resolverRevision", "Resolver revision must advance monotonically.");
        }
        if (options.expectedTargetPlatform.empty() || options.expectedTargetAbi.empty()
            || manifest.targetPlatform != options.expectedTargetPlatform
            || manifest.targetAbi != options.expectedTargetAbi
            || (!m_targetPlatform.empty() && m_targetPlatform != options.expectedTargetPlatform)
            || (!m_targetAbi.empty() && m_targetAbi != options.expectedTargetAbi))
        {
            return MountIssue(outIssues, "catalog.target",
                "AssetSet platform/ABI does not match the host resolver target.");
        }

        MountedAssetSet mounted;
        mounted.mountId = mountId;
        mounted.byteSource = std::move(byteSource);
        if (!NormalizeAssetSetManifest(manifest, mounted.manifest, outIssues))
        {
            return false;
        }
        std::set<AssetIdentity> overrides;
        for (const AssetIdentity& identity : options.overrideIdentities)
        {
            if (!overrides.insert(identity).second)
            {
                return MountIssue(outIssues, "catalog.overrides",
                    "Duplicate override identity: " + MountedIdentityText(identity));
            }
        }
        mounted.definitionRevisions.reserve(mounted.manifest.entries.size());
        for (const AssetSetEntry& entry : mounted.manifest.entries)
        {
            const auto existing = m_mountedEntries.find(entry.asset.key);
            const bool overrideRequested = overrides.erase(entry.asset.key) != 0u;
            if (existing == m_mountedEntries.end())
            {
                if (overrideRequested)
                {
                    return MountIssue(outIssues, "catalog.overrides", "Override has no mounted definition: "
                        + MountedIdentityText(entry.asset.key));
                }
                mounted.definitionRevisions.push_back(0u);
                continue;
            }

            const MountedEntryLocation& location = existing->second;
            const MountedAssetSet& previousMount = m_mounts[location.mountIndex];
            const AssetSetEntry& previous = previousMount.manifest.entries[location.entryIndex];
            if (SameMountedDefinition(previousMount.manifest, previous, mounted.manifest, entry))
            {
                if (overrideRequested)
                {
                    return MountIssue(outIssues, "catalog.overrides",
                        "Override names an identical, non-conflicting definition: "
                        + MountedIdentityText(entry.asset.key));
                }
                mounted.definitionRevisions.push_back(previousMount.definitionRevisions[location.entryIndex]);
                continue;
            }
            if (!overrideRequested)
            {
                return MountIssue(outIssues, "catalog.conflict." + MountedIdentityText(entry.asset.key),
                    "Conflicting definitions require an explicit identity override. Existing: "
                    + MountedSourceText(previousMount.mountId, previousMount.manifest, previous) + "; incoming: "
                    + MountedSourceText(mountId, mounted.manifest, entry));
            }
            mounted.definitionRevisions.push_back(nextResolverRevision);
        }
        if (!overrides.empty())
        {
            return MountIssue(outIssues, "catalog.overrides", "Override identity is absent from the incoming AssetSet: "
                + MountedIdentityText(*overrides.begin()));
        }

        CookedAssetCatalog candidate = *this;
        candidate.m_mounts.push_back(std::move(mounted));
        candidate.m_resolverRevision = nextResolverRevision;
        candidate.m_targetPlatform = options.expectedTargetPlatform;
        candidate.m_targetAbi = options.expectedTargetAbi;
        candidate.RebuildMountedIndex();
        AssetCatalogLookupIssue lookupIssue;
        if (!candidate.ValidateMountedHardGraph(lookupIssue))
        {
            return MountIssue(outIssues, "catalog.hardClosure", std::move(lookupIssue.message));
        }
        outCatalog = std::move(candidate);
        return true;
    }

    bool CookedAssetCatalog::WithoutMountedAssetSet(AssetDepot::AssetMountId mountId,
        std::uint64_t nextResolverRevision, CookedAssetCatalog& outCatalog,
        std::vector<AssetManifestIssue>& outIssues) const
    {
        outIssues.clear();
        if (&outCatalog == this)
        {
            return MountIssue(outIssues, "catalog.unmount",
                "The immutable candidate must not alias its input snapshot.");
        }
        if (!mountId.IsValid() || !IsMounted(mountId))
        {
            return MountIssue(outIssues, "catalog.mountId", "Mount ID is not active in this resolver snapshot.");
        }
        if (nextResolverRevision <= m_resolverRevision)
        {
            return MountIssue(outIssues, "catalog.resolverRevision", "Resolver revision must advance monotonically.");
        }

        CookedAssetCatalog candidate = *this;
        std::erase_if(candidate.m_mounts, [mountId](const MountedAssetSet& mount)
        {
            return mount.mountId == mountId;
        });
        candidate.m_resolverRevision = nextResolverRevision;
        candidate.RebuildMountedIndex();
        // Unlike mount, explicit logical removal may break surviving external
        // hard edges. New closure lookup must fail instead of borrowing a removed
        // mount from an old snapshot. No source unmap/delete occurs here.
        outCatalog = std::move(candidate);
        return true;
    }

    void CookedAssetCatalog::RebuildMountedIndex()
    {
        m_mountedEntries.clear();
        for (std::size_t mountIndex = 0u; mountIndex < m_mounts.size(); ++mountIndex)
        {
            const MountedAssetSet& mount = m_mounts[mountIndex];
            for (std::size_t entryIndex = 0u; entryIndex < mount.manifest.entries.size(); ++entryIndex)
            {
                const AssetSetEntry& entry = mount.manifest.entries[entryIndex];
                const MountedEntryLocation location{ mountIndex, entryIndex };
                const auto [existing, inserted] = m_mountedEntries.emplace(entry.asset.key, location);
                if (inserted)
                {
                    continue;
                }
                const MountedEntryLocation& previousLocation = existing->second;
                const MountedAssetSet& previousMount = m_mounts[previousLocation.mountIndex];
                const std::uint64_t previousRank = previousMount.definitionRevisions[previousLocation.entryIndex];
                const std::uint64_t incomingRank = mount.definitionRevisions[entryIndex];
                if (incomingRank > previousRank
                    || (incomingRank == previousRank && mount.mountId < previousMount.mountId))
                {
                    existing->second = location;
                }
            }
        }
    }

    ResolvedAssetEntry CookedAssetCatalog::ResolveMountedEntry(const MountedEntryLocation& location) const
    {
        const MountedAssetSet& mount = m_mounts[location.mountIndex];
        const AssetSetEntry& entry = mount.manifest.entries[location.entryIndex];
        return { mount.mountId, mount.manifest.assetSetId, mount.manifest.revision,
            m_resolverRevision, entry, mount.manifest.blobs[entry.blobIndex], mount.byteSource };
    }

    AssetLookupStatus CookedAssetCatalog::Find(const TypedAssetReference& asset, ResolvedAssetEntry& out) const
    {
        const TypedAssetReference reference = asset;
        out = {};
        const auto found = m_mountedEntries.find(reference.key);
        if (found == m_mountedEntries.end())
        {
            return AssetLookupStatus::NotMounted;
        }
        const MountedEntryLocation& location = found->second;
        if (m_mounts[location.mountIndex].manifest.entries[location.entryIndex].asset.kind != reference.kind)
        {
            return AssetLookupStatus::TypeMismatch;
        }
        out = ResolveMountedEntry(location);
        return AssetLookupStatus::Found;
    }

    std::vector<TypedAssetReference> CookedAssetCatalog::ListRoots(
        AssetDepot::AssetMountId mountId, CookedAssetKind kind) const
    {
        std::vector<TypedAssetReference> roots;
        const auto mount = std::ranges::find_if(m_mounts, [mountId](const MountedAssetSet& mounted)
        {
            return mounted.mountId == mountId;
        });
        if (mount == m_mounts.end())
        {
            return roots;
        }
        for (const TypedAssetReference& root : mount->manifest.roots)
        {
            if (root.kind == kind)
            {
                roots.push_back(root);
            }
        }
        return roots;
    }

    AssetLookupStatus CookedAssetCatalog::TraverseHardClosure(const TypedAssetReference& root,
        std::map<AssetIdentity, std::uint8_t>& states, std::vector<ResolvedAssetEntry>* outOrdered,
        AssetCatalogLookupIssue& outIssue) const
    {
        struct Frame final
        {
            MountedEntryLocation location{};
            std::size_t nextDependency{};
        };
        std::vector<Frame> stack;
        const auto fail = [&](AssetLookupStatus status, const TypedAssetReference& asset)
        {
            outIssue = {};
            outIssue.status = status;
            outIssue.asset = asset;
            for (const Frame& frame : stack)
            {
                outIssue.dependencyPath.push_back(
                    m_mounts[frame.location.mountIndex].manifest.entries[frame.location.entryIndex].asset);
            }
            outIssue.dependencyPath.push_back(asset);
            if (status == AssetLookupStatus::NotMounted)
            {
                outIssue.message = "Hard dependency is NotMounted: ";
            }
            else if (status == AssetLookupStatus::TypeMismatch)
            {
                outIssue.message = "Hard dependency type mismatch: ";
            }
            else
            {
                outIssue.message = "Hard dependency ownership cycle: ";
            }
            bool first = true;
            for (const TypedAssetReference& pathAsset : outIssue.dependencyPath)
            {
                if (!first)
                {
                    outIssue.message += " -> ";
                }
                first = false;
                outIssue.message += MountedIdentityText(pathAsset.key) + " (kind "
                    + std::to_string(static_cast<unsigned>(pathAsset.kind)) + ")";
            }
            return status;
        };
        const auto enter = [&](const TypedAssetReference& asset)
        {
            const auto found = m_mountedEntries.find(asset.key);
            if (found == m_mountedEntries.end())
            {
                return fail(AssetLookupStatus::NotMounted, asset);
            }
            const MountedEntryLocation& location = found->second;
            if (m_mounts[location.mountIndex].manifest.entries[location.entryIndex].asset.kind != asset.kind)
            {
                return fail(AssetLookupStatus::TypeMismatch, asset);
            }
            std::uint8_t& state = states[asset.key];
            if (state == 1u)
            {
                return fail(AssetLookupStatus::HardDependencyCycle, asset);
            }
            if (state == 0u)
            {
                state = 1u;
                stack.push_back({ location, 0u });
            }
            return AssetLookupStatus::Found;
        };

        AssetLookupStatus status = enter(root);
        if (status != AssetLookupStatus::Found)
        {
            return status;
        }
        while (!stack.empty())
        {
            Frame& frame = stack.back();
            const AssetSetEntry& entry =
                m_mounts[frame.location.mountIndex].manifest.entries[frame.location.entryIndex];
            if (frame.nextDependency == entry.dependencies.size())
            {
                states[entry.asset.key] = 2u;
                if (outOrdered)
                {
                    outOrdered->push_back(ResolveMountedEntry(frame.location));
                }
                stack.pop_back();
                continue;
            }
            const AssetDependency& edge = entry.dependencies[frame.nextDependency++];
            if (edge.kind == AssetDependencyKind::Hard)
            {
                status = enter(edge.target);
                if (status != AssetLookupStatus::Found)
                {
                    return status;
                }
            }
        }
        return AssetLookupStatus::Found;
    }

    bool CookedAssetCatalog::ValidateMountedHardGraph(AssetCatalogLookupIssue& outIssue) const
    {
        outIssue = {};
        // Every advertised root must retain its expected kind under an explicit
        // override. Missing loadable targets are allowed, but a mounted target
        // of the wrong kind is already a known reference-integrity error.
        for (const MountedAssetSet& mount : m_mounts)
        {
            for (const TypedAssetReference& root : mount.manifest.roots)
            {
                const auto found = m_mountedEntries.find(root.key);
                const MountedEntryLocation& location = found->second;
                if (m_mounts[location.mountIndex].manifest.entries[location.entryIndex].asset.kind != root.kind)
                {
                    outIssue.status = AssetLookupStatus::TypeMismatch;
                    outIssue.asset = root;
                    outIssue.dependencyPath = { root };
                    outIssue.message = "Mounted root type mismatch: " + MountedIdentityText(root.key)
                        + " declared by mount " + std::to_string(mount.mountId.value);
                    return false;
                }
            }
        }
        std::map<AssetIdentity, std::uint8_t> states;
        for (const auto& [identity, location] : m_mountedEntries)
        {
            const AssetSetEntry& entry = m_mounts[location.mountIndex].manifest.entries[location.entryIndex];
            for (const AssetDependency& edge : entry.dependencies)
            {
                if (edge.kind != AssetDependencyKind::Loadable)
                {
                    continue;
                }
                const auto target = m_mountedEntries.find(edge.target.key);
                if (target != m_mountedEntries.end())
                {
                    const MountedEntryLocation& targetLocation = target->second;
                    const AssetSetEntry& targetEntry =
                        m_mounts[targetLocation.mountIndex].manifest.entries[targetLocation.entryIndex];
                    if (targetEntry.asset.kind != edge.target.kind)
                    {
                        outIssue.status = AssetLookupStatus::TypeMismatch;
                        outIssue.asset = edge.target;
                        outIssue.dependencyPath = { entry.asset, edge.target };
                        outIssue.message = "Mounted loadable dependency type mismatch: "
                            + MountedIdentityText(entry.asset.key) + " -> " + MountedIdentityText(edge.target.key);
                        return false;
                    }
                }
            }
            if (TraverseHardClosure(entry.asset, states, nullptr, outIssue) != AssetLookupStatus::Found)
            {
                return false;
            }
        }
        return true;
    }

    AssetLookupStatus CookedAssetCatalog::CollectHardClosure(const TypedAssetReference& root,
        std::vector<ResolvedAssetEntry>& outOrdered, AssetCatalogLookupIssue& outIssue) const
    {
        const TypedAssetReference reference = root;
        outOrdered.clear();
        outIssue = {};
        std::map<AssetIdentity, std::uint8_t> states;
        std::vector<ResolvedAssetEntry> candidate;
        const AssetLookupStatus status = TraverseHardClosure(reference, states, &candidate, outIssue);
        if (status == AssetLookupStatus::Found)
        {
            outOrdered.swap(candidate);
        }
        return status;
    }
}
