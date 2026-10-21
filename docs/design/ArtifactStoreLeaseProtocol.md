# Local artifact backing leases

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/artifact_store_lease_probe.cpp:60](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/artifact_store_lease_probe.cpp#L60) — `Require(ArtifactStoreGuard::BeginPublication(root, publication, failure), failure);`
- [Tools/regression/artifact_store_lease_probe.cpp:70](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/artifact_store_lease_probe.cpp#L70) — `Require(ArtifactStoreGuard::TryAcquireCollection(root, collection, failure) == ArtifactStoreAccess::Busy,`
- [Tools/regression/artifact_store_lease_probe.cpp:225](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/artifact_store_lease_probe.cpp#L225) — `own::make_shared<Cooked::LooseArtifactByteSource>(root);`
- [BuildTool/Paths.cs:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Paths.cs#L9) — `internal static class Paths`
- [BuildTool/Paths.cs:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Paths.cs#L24) — `if (part.EndsWith(' ') || part.EndsWith('.') || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||`
- [BuildTool/Paths.cs:50](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Paths.cs#L50) — `var length = handle.IsInvalid ? 0 : GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);`
- [Engine/Utility_Framework/ArtifactStoreGuard.h:71](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ArtifactStoreGuard.h#L71) — `[[nodiscard]] static ArtifactStoreAccess TryAcquireCollection(const std::filesystem::path& backing,`
- [Engine/Utility_Framework/ArtifactStoreGuard.h:80](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ArtifactStoreGuard.h#L80) — `[[nodiscard]] static bool BeginPublication(const std::filesystem::path& backing,`
- [Engine/Utility_Framework/ArtifactStoreGuard.h:115](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ArtifactStoreGuard.h#L115) — `[[nodiscard]] bool CommitPublication(std::string& failure)`
- [Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h#L16) — `class PakAudioClipByteSource final : public ArtifactByteSource`
- [Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h#L19) — `explicit PakAudioClipByteSource(std::shared_ptr<const Pak::Archive> archive)`
- [Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h#L20) — `: archive_(std::move(archive)) {}`
- [Tools/AssetPacker/AssetPacker.cpp:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetPacker/AssetPacker.cpp#L29) — `throw std::runtime_error(message);`
- [Tools/AssetPacker/AssetPacker.cpp:48](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetPacker/AssetPacker.cpp#L48) — `if (raw.empty()) Fail(std::string(label) + " path is empty");`
- [Tools/AssetPacker/AssetPacker.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetPacker/AssetPacker.cpp#L53) — `Fail(std::string(label) + " uses a device/extended path namespace: " +`
- [Engine/RenderEngine/AssetDepot/AssetDepot.cpp:177](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetDepot.cpp#L177) — `own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource,`
- [Engine/RenderEngine/AssetDepot/AssetDepot.cpp:191](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetDepot.cpp#L191) — `own::shared_owner<const cooked::CookedAssetCatalog> snapshot;`
- [Engine/RenderEngine/AssetDepot/AssetDepot.cpp:239](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetDepot.cpp#L239) — `auto published = own::make_shared<const cooked::CookedAssetCatalog>(std::move(candidate));`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

This implementation is a bounded physical-exclusion slice of the approved
AssetDepot ownership contract. It does not implement a garbage collector, select
obsolete releases, delete files, repack archives, or infer GPU completion.

## Existing storage roles

- `LooseArtifactByteSource` opens one shared OS guard when constructed. Catalog
  snapshots, requests, and unselected lazy-child origins already retain that
  source, so they protect the immutable release without opening every artifact
- A narrowed `SingleArtifactSource` retains its selected native file and shares
  the release guard. It does not retain the mount source, sibling file handles,
  or decoded payload bytes. Physical collection is conservatively release-wide
- `Pak::Archive` holds its own guard before opening the pak. This covers archives
  that outlive an individual `PakAudioClipByteSource`; existing native FILE RAII
  and chunk-range reads are unchanged
- A guard is native OS RAII. Engine lifetime sharing uses the existing public
  `own::make_shared` and `own::shared_owner` APIs. There is no new manager,
  distributed lease service, or public per-asset lease wrapper

## Publication and identity

`ArtifactStoreGuard::BeginPublication` reserves a new backing path and holds its
exclusive sibling guard. The producer creates/validates a candidate, atomically
publishes it, then calls `CommitPublication` before reporting success. The
persistent guard record binds the normalized backing path to its volume/device,
file identity, directory/file kind, and backing incarnation stamp (Windows
creation time; POSIX ctime seconds and nanoseconds). The guard file's own ctime
is deliberately excluded from its identity because publication writes it.
Existing backing cannot be retroactively
enrolled, because legacy readers may lack the protocol.

`BuildAssetSet` performs these publication steps for new immutable loose outputs.
The guard is a sibling named `<backing>.asset-store.guard`, outside the release
and its payload list. It is machine-local coordination metadata, not a portable
artifact or input to deterministic build keys. Moving/copying a release requires
fresh publication at a new path; copied guard records do not authorize GC of a
new file identity. Safe deployment reserves the absent destination with
`BeginPublication`, copies into a private candidate, renames it to the destination,
calls `CommitPublication`, then releases the exclusive publication guard before
mounting. Already-extracted final roots remain unmanaged/noncollectible; there is
no retrofit enrollment operation. Existing `AssetPacker` replace-in-place outputs are not silently
converted. Immutable pak producers can use the same new-path publication API.

Never delete, move, truncate, or replace a persistent guard as part of collection.
It must remain in a stable parent directory. Abandoned publication records are
not evidence of a live owner; a new publication may acquire the existing OS guard
only when the target path is still absent. An incomplete record fails closed for
readers and collection, and cannot be repaired by inferring process age.

Paths crossing symlinks/reparse points and multiply-linked file backing/guards
are rejected. Windows canonical naming also resolves short-name aliases before
choosing a sidecar. Shared acquisition validates the recorded backing identity;
source reads that would reopen a loose path validate the root again. Pak open
also compares its native FILE identity with the acquired guard. On POSIX,
changing an enrolled immutable root's metadata invalidates its captured incarnation even when
the inode is unchanged. Publish a new immutable root rather than touching an
existing enrolled root. Unmanaged legacy roots still validate directory/file
identity, but do not enforce the managed incarnation contract for unrelated
metadata changes. These checks harden ordinary replacement/inode reuse;
they do not claim to defeat adversarial timestamp restoration.

## Physical cleanup eligibility

1. Separately establish that an exact release/pak is obsolete under publication
   policy. This API does not decide whether a release will be needed in future
2. Call `TryAcquireCollection(backing, capability, failure)` in a storage worker
3. `Busy` means a mount, old snapshot, descriptor, payload origin, job, archive,
   another producer, or another collector still owns an OS guard. `Unmanaged`
   means no enrollment exists and expressly does not allow collection. `Invalid`
   includes incomplete/corrupt metadata, aliases, and changed backing identity
4. Only `Acquired` returns a held exclusive capability. Keep it alive throughout
   eventual physical work, use its exact `BackingPath`, and call
   `RevalidateCollection` immediately before mutation. Revalidation refuses a
   replaced root/pak/guard. Never turn the result into a cached Boolean
5. There is deliberately no destructive API in this slice. A future collector
   must use this protocol for the whole backing and leave the sibling guard
   intact. Ancestor-directory cleanup must first cover every descendant store;
   taking an unrelated ancestor's guard is not permission to remove descendants

Windows uses `LockFileEx`; POSIX uses `flock` on separate close-on-exec descriptors.
Locks are nonblocking and cover separate opens in the same process as well as
independent cooperating processes. OS process termination releases locks, so no
PID expiry file, `use_count()`, GPU token, timeout, or stale-file heuristic decides
whether owners exist. This protocol is for a supported local filesystem; it does
not claim distributed/network-filesystem lease semantics. Uncooperative external
mutation can still cause explicit identity/I/O errors and is not made safe by an
OS advisory protocol.

Shared acquisition is metadata I/O at source/archive construction. It must not
be added to `TryAcquire`, resident cache-hit paths, or GT/RT uploads. Logical
unmount only drops the new resolver's source references; it does not release
references held by old snapshots, descriptors, or jobs.

## Verification status

`Tools/regression/artifact_store_lease_probe.cpp` is added but has not been built
or run. It covers mounted/retired snapshots, never-read lazy origins, narrowed
file captures without sibling descriptors, same-process and independent-process
exclusion, OS release after child termination, missing/corrupt enrollment,
replacement identity and same-inode incarnation rejection, and Windows pak archive lifetime. Linux checks
native descriptor counts. Existing catalog/source regressions remain relevant.
No deletion, repack, build, binary, shader compilation, or test was executed as
part of this implementation task. Static source review is not a runtime pass.

</details>
