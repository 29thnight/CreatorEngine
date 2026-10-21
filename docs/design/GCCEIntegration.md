# GCCE scene graph integration

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- SceneManager는 GCCE domain과 Scene root를 소유한다. Scene→Entity→Component의 trace 참조와 논리적 제거·핸들 무효화를 분리한다.
- 일반 new/unique_ptr 기반 Scene 그래프를 새 이행 목표로 사용하지 않는다. 자산·SDK·GPU 수명은 독립 owner/완료 계약을 따른다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/SceneRuntime/SceneGC.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneGC.h#L1) — `#pragma once`
- [Tools/regression/GCCEProbe.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/GCCEProbe.ps1#L5) — `[Parameter(Mandatory)][string]$Repository,`
- [Tools/regression/GCCEProbe.ps1:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/GCCEProbe.ps1#L6) — `[Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration`
- [ScriptCore/Prefab.cs:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L10) — `public readonly struct Prefab`
- [ScriptCore/Prefab.cs:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L12) — `private readonly string? _name;`
- [ScriptCore/Prefab.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L14) — `private Prefab(string name) => _name = name;`
- [ScriptCore/Animator.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L14) — `public sealed class Animator : NativeComponent`
- [ScriptCore/Animator.cs:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L18) — `public void SetBool(string name, bool value) => Native.AnimatorSetBool(OwnerHandle, name, value);`
- [ScriptCore/Animator.cs:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L19) — `public void SetFloat(string name, float value) => Native.AnimatorSetFloat(OwnerHandle, name, value);`
- [ScriptCore/MeshRenderer.cs:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L13) — `public sealed class MeshRenderer : NativeComponent`
- [ScriptCore/MeshRenderer.cs:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L15) — `public string MaterialName => Native.MeshGetMaterialName(OwnerHandle);`
- [ScriptCore/MeshRenderer.cs:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L24) — `public void InstantiateMaterial(string newName) => Native.MeshInstantiateMaterial(OwnerHandle, newName);`
- [Engine/SceneRuntime/Prefab.h:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.h#L10) — `class [[reflgen::reflect]] Prefab : public Object`
- [Engine/SceneRuntime/Prefab.h:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.h#L12) — `friend struct reflgen::access;`
- [Engine/SceneRuntime/Prefab.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.h#L18) — `static Prefab* CreateFromGameObject(const Entity* source, std::string_view name = "");`
- [Engine/SceneRuntime/Socket.h:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.h#L38) — `void AttachObject(Entity* Object);`
- [Engine/SceneRuntime/Socket.h:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.h#L39) — `void DetachObject(Entity* Object);`
- [Engine/SceneRuntime/Socket.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.h#L40) — `void DetachAllObject();`
- [Engine/SceneRuntime/Animator.h:81](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L81) — `own::shared_owner<const assets::ModelAnimationPayload> payload{};`
- [Engine/SceneRuntime/Animator.h:112](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L112) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/Animator.h:114](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L114) — `Component::gc_trace(tracer);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Status: scoped Windows Debug/Release validation, 2026-10-09. Scene GC repeated
20 times per configuration and Play round-trip regressions passed. See the
[validation record](../analysis/GCCEValidation20261009.md) for evidence and remaining gates.

## Ownership and construction

The scope is Scene → Entity → Component. `Object` is deliberately not managed:
Prefab and other non-GC objects retain their existing ownership. AssetDepot and
`ownership_cpp` resource owners, native SDK handles, renderer snapshots, and GPU
accepted-work/completion ownership are unchanged.

- SceneManager owns one GameThread `gc::domain`, declared before its external
  `root_ref<Scene>` registry so the domain is destroyed last. Its public Scene list
  is a const, nonowning enumeration. Temporary loading and DDOL transfers hold roots.
- Scene traces its Entity slots; Entity traces its ordered Component edges and the
  eagerly allocated missing-Transform fallback. SceneStore indexes/generations,
  hierarchy transactions, and DDOL identity remain authoritative.
- Component observes its owner through a weak identity. `GetOwner()` remains a raw
  frame-borrow accessor for joined workers and fenced presentation reads; it does
  not promote a weak reference on those threads.
- Scene factories take the domain and return a root. The manager adopts that root.
  Entity factories publish through `gc::make` before attaching Components. This
  two-phase construction makes owner identity available before `SetOwner`.
- Engine Components use `Component::CreateManaged`, which creates an external
  temporary root and establishes a cleanup obligation immediately. Publication
  into Entity precedes scheduler registration; failure rolls back both.
- GC objects must not be created on the stack, embedded by value, made by ordinary
  `new`, or held by `unique_ptr`. Socket pose data is a plain matrix value, not a
  Transform Component. Standalone probes explicitly finalize before dropping roots.

## Tracing and reflection

Tracing is explicit and independent of serialization. Every shipped concrete
Component declares its own `gc_trace`, calls its base hook, and visits any strong
GC fields, including ignored/nonserialized fields. The typed Component factory
rejects a new subtype that only inherits a trace hook. This is a source contract,
not automatic detection of an omitted field.

Graph references retain their prior serialized pointer/sequence shape. Generic
property copying and graph deserialization do not allocate or reassign ownership;
Scene and ComponentFactory perform restoration in the explicit domain. The closed
factory uses the existing `REFLECT_TYPE_LIST` and type IDs. Public CLR/native ABI
handles remain generation values, never `gc::weak_ref` values that could outlive a
domain. Editor cross-frame selections/popups re-resolve scene/entity/component IDs.

The pinned reflgen 1.0.0 descriptor factory previously checked only default
constructibility before instantiating `new T` and `delete T`. The local vcpkg port
patch additionally requires legal class allocation/deletion. Managed types still
have metadata/serialization, but no ordinary descriptor factory. No reflgen
repository changes or new code-generation protocol are required.

## Thread and borrow boundaries

| Entry | Execution and retained data |
| --- | --- |
| Runtime/CLR graph operations | GameThread; Scene roots and traced edges |
| Editor create/delete/duplicate/reparent/component/prefab operations | PresentationThread queues owned values and generation handles; existing GameThread command pump applies them |
| Light creation and configuration | One queued operation; no fabricated immediate creation result |
| Undo/Redo replay | GameThread; value-only Undo commits retain their existing synchronous UI behavior |
| UI factories, hierarchy paste, terrain attachment, prefab drops | Whole actions queued with owned paths/IDs; targets resolved at execution |
| Prefab editor open/close | GameThread adoption/retirement; persisted identities are re-resolved |
| Editor command delivery/polling | Separate orchestration and scene-borrow sections; no blocking PresentationThread future |
| Animation and transform workers | Existing jobs join before graph edges may detach or collection runs; stable Scene-rooted graph supplies their borrow lifetime |
| AI jobs | Existing immutable snapshots and EntityHandle values |
| Render/presentation/GPU work | Existing sealed snapshots and independent resource/completion owners |

Queued edits capture the scene-context epoch as well as target generations. A
scene/session transition rejects an old click instead of applying it to a new
context. Queue admission closes before teardown. No GC root or pin is moved into a
worker/editor closure whose destructor could run on another thread. A pin protects
memory, not logical Destroy state or unsynchronized data writes. An asynchronous
borrow that can outlive its graph edge needs a pin for the actual borrowed object,
retained and released by the owner thread after its job joins.

Live-scene command handlers must not synchronously wait for PresentationThread
work while holding the scene borrow fence. Use request/poll splitting for such
operations. Source review found bounded RT/GPU waits and nonblocking presentation
polls, not a demonstrated GT/PT wait cycle. This is not proof of runtime deadlock
freedom, and some existing diagnostic handlers still block while they execute.

## Deterministic cleanup and collection

Destroy still delivers the existing lifecycle hooks. Pending cleanup keeps objects
reachable. Script handles remain valid during final hooks and are invalidated before
Entity edge removal; DDOL detach does not invalidate them. Selection borrows are
pruned before a Scene slot is released. Detached components leave the source
lifecycle schedule and re-register in the destination.

`FinalizeManagedDestroy` performs idempotent native resource release before clearing
owner observations and marking the GC lifecycle destroyed. Animator releases CLR
animation instances, controller owners, sockets, asset requests, and its native
instance record there; MeshRenderer/Foliage cancel their subscriptions there.
Collector destructors release owned storage, not normal engine lifecycle callbacks.
The current CLR animation release is `AniBehaviorFactory.Destroy` dictionary removal,
not a user Enter/Exit callback. Future cleanup extensions must recheck reentrancy.

Scene retirement sets its guard before callbacks and rejects new graph mutations or
re-adoption. Cleanup failures retain their roots and fail closed; they are not marked
successfully retired. Scene slot reuse still invalidates generation handles.

Collection runs after EndOfFrame callbacks and the DDOL observer sweep, while the
editor's existing scene/presentation fence is held. The Player has the equivalent
post-EndOfFrame boundary. Normal collection is `collect_step`: 250 μs soft budget,
16 minimum units, 1 MiB allocation trigger, and a one-second maximum cycle interval.
Teardown/edge release explicitly requests collection. No full collection runs every
frame. Root scans, a large tracer/destructor, and allocator maintenance may overrun
the soft budget; no latency or performance improvement is claimed.

Shutdown order is stop admission → drain loads/AI/jobs and PT/RT consumers → run
Scene/component hooks and invalidate handles → drop graph/transfer roots → full
collection → clear callback roots and collect again → shut down CLR/audio/other
services. The domain is destroyed last. Module object count alone is not enough to
unload code; callbacks and workers must also be quiescent.

The default GCCE violation handler still aborts. Quarantine is not a substitute for
cleanup, root release does not itself request collection, and nonmoving allocator
pools may retain warm committed memory. No returning production violation handler
has been installed to hide lifetime errors.

## Dependency and packaging

[Upstream provenance](../../ThirdParty/GCCE/PROVENANCE.md) pins all 16 unmodified
source files to `aef79b3f57861fc16d0045e860e9082eeed8ec29` with hashes. Upstream
supplies no LICENSE at this revision; none has been invented or changed.

`Engine/GCCE/GCCE.vcxproj` builds one C++23 DLL. Only that project defines
`GCCE_BUILDING`; consumers use `GCCE_SHARED` and matching `GC_DEBUG_CHECKS` (Debug 1,
Release 0). SceneGC.h is the engine entry header. The DLL opts out of reflgen and
vcpkg and uses the matching shared CRT. This exported C++ interface requires matching
toolchain/configuration, not a stable cross-version C ABI.

GCCE is shipping-neutral: `Bin/<platform>-<configuration>/Runtime/Common/gcce.dll`
and `Build/Lib/<platform>-<configuration>/gcce.lib` are shared by Development and
Shipping, including identical intermediate/PDB paths. The existing launcher and
hashed deployment/package manifests carry its import closure. Standalone compiler
probes use GCCEProbe.ps1 for the same definitions, import library, and DLL lookup.
The vendored CMakeLists is provenance; its unvendored upstream tests/benchmarks are
not an engine build entrypoint.

## Validation and remaining gates

Initial integration review covered source, exact upstream blob hashes, changed
project XML, Python source parsing, include/reference inspection and whitespace.
Subsequent authorized validation built the Editor dependency closure and managed
assemblies in Windows x64 Debug/Release. Editor GC owner-source contracts (8),
Entity ownership contracts, reflection container classification (D/R), and
reflection value/shape round-trip (Debug, 37 axes) passed.

Regression source includes `SceneGCSelfTest` (real graph cycles, hooks, stale handles,
selection, DDOL and an actual component pin through job join), editor owner-thread
source contracts, and migrated existing standalone component probes. The Scene GC
fixture is invoked by the process-scoped `scene.gc.selftest` commandlet under the
host scene borrow fence. Debug/Release each passed 20 repetitions with exit 0,
including final incremental collection and unchanged live/quarantine/aborted-cycle
counts. Its pin example intentionally distinguishes storage retention from
logical cleanup and does not claim pins prevent Destroy.

Play-to-Stop content restoration and the W5 state-machine/failure-injection/Undo
preservation gates also passed in both configurations. These scoped initialized
host runs exercise DLL loading, reflection regeneration, script handle identity
and normal shutdown. They do not complete general script/native ABI coverage.
Remaining runtime gates include repeated prefab UI transitions, cleanup callback
reentrancy/failure paths, pending-work shutdown, packaged Player/Shipping,
NativeAOT publish, ASan, and scale/latency benchmarks. Upstream tests were not run.
The validation record also preserves Release PDB and profiler shutdown warnings.

</details>
