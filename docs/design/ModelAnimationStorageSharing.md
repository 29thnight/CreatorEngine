# Compatible skeleton and clip decode storage

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/model_animation_storage_probe.cpp:282](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/model_animation_storage_probe.cpp#L282) — `m_data->SetModelAnimationStorageCacheBudgets(0u, 0u);`
- [Tools/regression/model_animation_storage_probe.cpp:285](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/model_animation_storage_probe.cpp#L285) — `&& snapshot.skeletonStorageLiveBytes == a->storage->ByteSize()`
- [Tools/regression/model_animation_storage_probe.cpp:316](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/model_animation_storage_probe.cpp#L316) — `bool PollReplacement()`
- [ScriptCore/Animator.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L14) — `public sealed class Animator : NativeComponent`
- [ScriptCore/Animator.cs:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L18) — `public void SetBool(string name, bool value) => Native.AnimatorSetBool(OwnerHandle, name, value);`
- [ScriptCore/Animator.cs:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L19) — `public void SetFloat(string name, float value) => Native.AnimatorSetFloat(OwnerHandle, name, value);`
- [Engine/SceneRuntime/Animator.h:81](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L81) — `own::shared_owner<const assets::ModelAnimationPayload> payload{};`
- [Engine/SceneRuntime/Animator.h:87](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L87) — `own::shared_owner<const assets::ModelSkeletonPayload> skeleton{};`
- [Engine/SceneRuntime/Animator.h:325](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L325) — `own::shared_owner<const assets::ModelSkeletonPayload> skeleton);`
- [Engine/RenderEngine/DataSystem.h:235](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L235) — `[[nodiscard]] own::shared_owner<const assets::ModelSkeletonPayload> TryAcquireSkeleton(`
- [Engine/RenderEngine/DataSystem.h:237](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L237) — `[[nodiscard]] own::shared_owner<const assets::ModelAnimationPayload> TryAcquireAnimation(`
- [Engine/RenderEngine/DataSystem.h:245](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L245) — `void SetModelAnimationStorageCacheBudgets(std::size_t skeletons, std::size_t clips);`
- [Engine/Utility_Framework/Ownership.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Ownership.h#L1) — `#pragma once`
- [Engine/RenderEngine/Assets/ModelSkeletonPayload.h:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Assets/ModelSkeletonPayload.h#L29) — `ModelSkeletonPayload(own::shared_owner<const ModelSkeletonStorage> decoded,`
- [Engine/RenderEngine/Assets/ModelSkeletonPayload.h:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Assets/ModelSkeletonPayload.h#L34) — `origin(std::move(resolved)), decodedBytes(charge) {}`
- [Engine/RenderEngine/Assets/ModelSkeletonPayload.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Assets/ModelSkeletonPayload.h#L40) — `const own::shared_owner<const ModelSkeletonStorage> storage;`
- [Engine/RenderEngine/Assets/ModelAnimationPayload.h:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Assets/ModelAnimationPayload.h#L34) — `ModelAnimationPayload(own::shared_owner<const ModelAnimationStorage> decoded,`
- [Engine/RenderEngine/Assets/ModelAnimationPayload.h:35](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Assets/ModelAnimationPayload.h#L35) — `own::shared_owner<const ModelSkeletonPayload> hardSkeleton,`
- [Engine/RenderEngine/Assets/ModelAnimationPayload.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Assets/ModelAnimationPayload.h#L40) — `origin(std::move(resolved)), decodedBytes(charge) {}`
- [Engine/SceneRuntime/Animator.cpp:26](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.cpp#L26) — `Animator::Animator() : m_instance(AnimatorSystems->CreateInstance()) {}`
- [Engine/SceneRuntime/Animator.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.cpp#L28) — `AnimInstance& Animator::GetInstance() noexcept`
- [Engine/SceneRuntime/Animator.cpp:576](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.cpp#L576) — `own::shared_owner<const assets::ModelSkeletonPayload> skeleton)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Approved ownership design implementation, isolated from `158479c`.
This slice is source-only: no builds, fixtures, runtime binaries, shader compilation,
or publication were run. The fixture below remains **unrun**.

## Ownership and compatibility

`ModelSkeletonStorage` and `ModelAnimationStorage` are immutable whole-artifact raw
decode owners. The existing `ModelAssetRuntimeState` contains their caches and work
entries; there is no additional manager. Weak lookup, bounded strong retention,
accepted raw single-flight jobs, and scheduler dependency tokens reuse the existing
model cache mechanics. Different logical IDs and resolver revisions can join a raw
flight. The key includes content SHA-256, encoded size, kind, representation,
schema, platform, ABI and decoder recipe. The source locator is excluded.

The skeleton artifact is identity-free. Clip wire bytes contain the required
skeleton AssetId, ordered-layout digest and bone count; only the full compatible
blob shares storage, never just matching keyframes. `BindModelAnimation` checks the
logical wrapper's own exact hard skeleton every time, including raw-cache hits.
It compares the typed declared edge, captured revision, skeleton identity, count,
and layout digest. Mesh skin-binding validation remains unchanged.

Logical `ModelSkeletonPayload`/`ModelAnimationPayload` retain their own complete
`origin`, and clips pin their own exact logical skeleton. Their sampling adapters
are const references into owned storage. `skeletonId`/`animationId` on those raw
adapters stay nil; use `origin.entry.asset` for logical identity. The two granular
Animator identity checks now do so; legacy whole-model adapters are unchanged.
A nonmovable clip storage owns the track table and the tracks it borrows from.
The storage member is a const owner so its wrapper references cannot be reseated.

The verified raw source and artifact path remain a matched pair in storage.
An alias's origin is not rewritten to another alias's source or path. Pinned old
model descriptors retain their original source metadata and rehydrate that exact
snapshot without asking the current resolver. Source-root lease and external
replacement/error contracts are unchanged.

## Requests, retirement and budgets

Logical work remains keyed by logical asset/blob/revision/exactness. Raw work is
exact-compatible work: logical invalidation or one consumer's cancellation cannot
cancel it or rewrite another wrapper's result. Each logical job depends on fixed
raw/skeleton completion tokens. There are no token replacements, worker waits,
automatic latest retries, or changes to the conditional collider predecessor graph.
Shutdown still drains accepted work; raw stores join the existing staged shutdown
retirement transaction.

Existing logical budgets remain 4 MiB descriptor, 16 MiB skeleton and 64 MiB clip.
Their `ByteSize` conservatively includes their shared storage/hard-skeleton pins.
Independent raw retention defaults to 16 MiB skeleton and 64 MiB clip, configured
by `SetModelAnimationStorageCacheBudgets`. These limits are additional retention
budgets, not an aggregate physical-memory cap. Set both budget APIs to zero to
release the retained owners of those animation caches. Separate skinned-mesh
cache entries may still retain skeleton storage; consumer, frame and accepted-work
owners also remain valid. Shared closure charges may conservatively overlap.

All model publishing paths and animation budget setters reserve retirement vectors
before changing state and move evicted owners into those vectors. Publishing work
owns the vectors through the scheduler terminal observer; budget setters release
them after unlock. Snapshot weak upgrades are similarly pinned until after unlock.
Logical eviction is not a report of physical deallocation.

## Diagnostic meaning

- `skeletonStorageLiveBytes` and `animationStorageLiveBytes` count each compatible
  raw storage once while its weak cache entry can acquire an owner. They include
  old logical generations after ordinary unmount/remount because raw keys survive
  logical invalidation. Shutdown detaches those entries, ending this indexed view
- `*StorageRetainedBytes` and `*StorageBudgetBytes` concern raw cache retention
  only. `retainedChargeBytes`/`budgetBytes` include all seven model caches
- `inputStagingBytes` sums the actual capacities of currently owned encoded read
  buffers. The counter is published after resize and cleared after destruction,
  including I/O failure, stale publication, cancellation and exceptions
- `decodeStagingChargeBytes` observes decoded Skeleton/Clip intermediate vector
  capacities once the bounded codec returns and while native output vectors are
  assembled, then the candidate's conservative `ByteSize` until publication. It
  excludes unexposed allocations inside codec parsing, hash validation and other
  native helpers, and allocator overhead; it is not a peak-memory instrument.
  Geometry reports the completed new candidate only, never an existing collider
  predecessor/resident handoff. Logical binding jobs add no bulk staging charge
- `completedWorkResultPinBytes` counts unique raw result objects pinned by completed
  work, including cache-hit dependency work held while another prerequisite is
  pending. This uses a weak work index that outlives logical cache invalidation;
  it does not add ownership. Ready-result work and live/retained categories can
  overlap and must not be added together as physical memory

Live/candidate `ByteSize` values are conservative object plus vector/string
capacity charges. They exclude cache/control-block metadata, allocator overhead,
source backing and GPU allocations. String capacity may include inline storage.
No request failure/cancel counters are introduced in this slice.

## Unrun focused fixture

`Tools/regression/model_animation_storage_probe.cpp` is a dedicated-host source
fixture, not a new executable target. Supply a running initialized DataSystem and
an instrumented host scheduler that can suspend **dispatch** without parking a
worker. Run `Begin` during suspended dispatch, resume, then tick `Poll` without
blocking. After it returns true, suspend dispatch, run `Replace`, resume, and tick
`PollReplacement` until true. Use a disposable DataSystem and tear it down only
after all requested completion tokens are terminal. The fixture's source is an
immutable in-memory artifact map; it counts verified artifact reads.

Covered assertions:

1. Actual key factory excludes only source path and separates content, size, kind,
   representation, schema, platform, ABI and decoder recipe
2. Distinct skeleton IDs/paths share one raw decode and retain distinct origins
3. Distinct clip IDs share one identical blob/storage/track table; changed clip
   content does not share, and an alias with a different declared hard skeleton
   fails even though its bones and raw clip storage are reusable
4. Cancelled subscriber stays Cancelled while a joined subscriber becomes Ready;
   accepted completion tokens eventually complete without waiting in the fixture
5. Zero retained budgets leave live externally pinned decoded storage visible
   exactly once; after dropping payloads the live bytes disappear
6. Read callbacks observe nonzero input staging and can take a cache snapshot,
   proving reads do not hold the preparation mutex; terminal work drains active
   staging/result-pin counters
7. A paused current request becomes terminal Stale on unmount. Old-descriptor
   rehydration and a replacement current generation can share clip storage while
   retaining different skeleton bind poses/revisions, and Stale is never replaced
   by the shared raw success

Additional authorized-run cases for the host fault injector:

- Fail raw submission/dispatch before the body and during dependency completion;
  assert all original logical completion tokens terminalize exactly once
- Complete raw clip work while its skeleton dependency remains dispatch-paused;
  trim both budgets, then snapshot. Completed result-pin bytes must remain nonzero
  with zero corresponding active input staging, including resident cache-hit work
- Pause a read, invalidate its logical entry and inspect the weak work index;
  input staging remains visible until the accepted raw read drains
- Inject allocation failure before publication/retirement reserve. Deliver an
  unretained result or terminal failure without destroying staged payloads under
  the preparation mutex; observe source destructor/lock ownership from the host
- Repeat the prior collider mixed positive/negative predecessor-chain fixtures to
  confirm the unchanged gate, token, cancellation and zero-budget handoff behavior

</details>
