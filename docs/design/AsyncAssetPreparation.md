# CPU asset preparation and scene publication

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/DataSystem.h:116](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L116) — `class DataSystem : public Singleton<DataSystem>`
- [Engine/RenderEngine/DataSystem.h:326](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L326) — `bool PublishPreparedModel(const ModelPreparation& preparation, std::string& error);`
- [Engine/RenderEngine/DataSystem.h:815](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L815) — `bool PublishRuntimeAsset(const own::shared_owner<PreparedRuntimeAsset>& asset, std::string& error);`
- [Engine/SceneRuntime/SceneManager.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L28) — `class SceneManager : public Singleton<SceneManager>`
- [Engine/SceneRuntime/SceneManager.h:350](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L350) — `static auto SceneManagers = SceneManager::GetInstance();`
- [Engine/SceneRuntime/SceneManager.h:352](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L352) — `static auto& PlayModeEvent = SceneManager::GetInstance()->PlayModeEvent;`
- [Engine/RenderEngine/Experiment/Model.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L9) — `class ModelLoader;`
- [Engine/RenderEngine/Experiment/Model.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L13) — `class Model final`
- [Engine/RenderEngine/Experiment/Model.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L15) — `public:`
- [Engine/RenderEngine/DataSystem.cpp:503](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L503) — `job_handle DataSystem::AssetBundlePreparation::Completion() const`
- [Engine/RenderEngine/DataSystem.cpp:719](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L719) — `bool DataSystem::PublishRuntimeAsset(const own::shared_owner<PreparedRuntimeAsset>& asset, std::string& error)`
- [Engine/RenderEngine/DataSystem.cpp:1343](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L1343) — `bool DataSystem::PublishPreparedModel(const ModelPreparation& preparation, std::string& error)`
- [Engine/SceneRuntime/SceneManager.cpp:1419](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L1419) — `SceneManager::SceneLoadRequestStatus SceneManager::QuerySceneLoad(std::uint64_t requestId) const`
- [Engine/SceneRuntime/SceneManager.cpp:1447](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L1447) — `Scene* SceneManager::BuildPreparedScene(const PendingSceneLoad& load)`
- [Engine/SceneRuntime/SceneManager.cpp:2243](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L2243) — `bool SceneManager::PreparePhysicsSceneExit(Scene* scene)`
- [Engine/RenderEngine/Experiment/Model.cpp:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.cpp#L14) — `std::uint64_t Model::NextGeneration() noexcept`
- [Engine/RenderEngine/Experiment/Model.cpp:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.cpp#L20) — `std::uint64_t Model::Generation() const noexcept { return generation_; }`
- [Engine/RenderEngine/Experiment/Model.cpp:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.cpp#L22) — `Model::Model(ModelDraft&& draft, ConstructKey) noexcept`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

The editor Open Scene action, `scene.load`, `scene.switch` and `scene.open_async`
queue CPU preparation and return `queued: true` plus a `requestId`. The load command
leaves activation unchanged; switch/open request the existing frame-boundary
activation. Query `scene.load.status <requestId>` for Pending, Ready, Failed,
Cancelled, Stale or Superseded. Ready means scene construction completed, not GPU
readiness or completed activation. The last 64 terminal value records are kept;
expired IDs report an explicit unknown-request error and retain no scene owners.

## Preparation boundary

`SceneManager` parses the document with the common job scheduler. `DataSystem`
walks the current scene and referenced material/prefab documents, including
encoded prefab overrides, inline material instances, bundle entries, named
sprite/UI textures, and foliage model references. A first open does not require
a previously written `ScenePreload` list.

1. Workers validate/decode immutable model candidates
2. The scene owner publishes completed model generations and discovers their
   generated material graph identities
3. Workers prepare graph programs, validation bytes and SHA-256 digests; the
   owner publishes those exact immutable results
4. Workers prepare the required CPU material/texture owners, using the existing
   material codecs and texture variant rules
5. The scene owner constructs entities and activates the prepared scene at the
   existing scene structure boundary

Scene loading and editor model placement share model preparation tickets.
Model placement keeps its existing incremental owner-thread application budget.
Graph preparation uses `GenerationStore::BeginPreparation`, `Prepare`, and
`Publish`; expensive compilation/verification does not hold the store mutex.
Requests for one asset revision share a candidate. An invalidated revision cannot
publish over the current owner, and a failure preserves the accepted generation.

The explicit synchronous `PrewarmSceneMaterials` path also retains generations.
It no longer discards a cooked program merely to populate the disk cache.
Subsequent entity loading consumes the prepared owner without decoding that
cache a second time. Generated compiler inputs are content-keyed and atomically
published so overlapping requests cannot observe another writer's truncation.

## Lifetime and scheduling

Preparation uses existing `job_handle` dependency chains. Asset lanes are bounded
by compiler capacity and leave two workers unreserved where the pool size allows.
A normal frame pump polls completion before reading a result; only explicit
synchronous/lifecycle APIs wait for incomplete jobs. Cancellation prevents scene
publication and does not attempt to preempt a compiler/decoder already running.

The request index is weak, so it does not keep another permanent copy of resident
model/graph owners. Superseded work remains drainable until its callbacks finish.
Asset changes invalidate request epochs; mount/root changes advance the captured
resolver revision. Every scene phase and final handoff validates both. Typed mesh,
image and material requests use the captured catalog. A short construction guard
blocks root publication using the existing admission mutex, while deserializers
and callbacks run without a held mutex. Asset-change callbacks defer their event
until the next drain. Candidate DDOL entities stay in the candidate scene, so a
rejected construction cannot leave them in the active scene. Once Ready handoff
succeeds, those actual scene owners remain valid through later mount changes.
Shutdown drains accepted work before clearing catalogs and caches.

Texture and Material runtime identities use one out-of-line atomic allocator.
They are nonzero, never recycled, and fail closed at exhaustion. Persisted
FileGuid/model identities and content fingerprints are unchanged. Material copy
and resource move identity behavior is unchanged.

## Readiness and remaining synchronous work

Progress counts CPU preparation items within the displayed stage, including
instance recipes; it is not a count of unique source assets. Dependency discovery
has no known total. Shader progress counts live compiler requests rather than
letting one completed request hide others.

CPU preparation is distinct from GPU readiness. Texture upload, fences, resource
retirement, PSO creation, and render-thread scene lookup initialization retain
their existing ownership and readiness contracts. Entity construction remains on
the scene owner and may still take a long frame. A small shared worker pool can
also delay other jobs while non-preemptible decoding/compilation is running.

Player startup and scene commands use nonblocking preparation. Explicit legacy
C++ scene helpers, watcher hot-reload acceptance and Material Graph authoring
apply/preview remain separate synchronous entry points. This does not claim zero
game-thread stalls or move mixed RHI initialization onto arbitrary workers.

## Verification scope

The patch was reviewed statically. Added regression source covers prepared-owner
reuse, same-revision coalescing, dirty reload retry, late completion rejection,
unchanged-product identity, removal and shutdown. Builds, regression execution,
engine execution, and performance measurements are still required before a
runtime/performance claim.

</details>
