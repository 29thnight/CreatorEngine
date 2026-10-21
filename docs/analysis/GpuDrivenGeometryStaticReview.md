# GPU-driven geometry: static review checkpoint

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

이 문서는 연구/운영/표현 가이드 성격이므로, 아래 파일은 관련 엔진 경계의 근거다. 문서의 제안 전체가 구현됐다는 증거로 해석하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:78](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L78) — `enum class RGMeasurementDomain : uint8_t { Normal, Capture };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:79](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L79) — `enum class RGQueueFallbackReason : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L108) — `enum class RGQueuePlacement : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L34) — `std::free(value);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:43](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L43) — `const auto path = prefix + "." + std::to_string(++serial);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:67](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L67) — `std::copy_n(source.data(), source.size(), destination.data());`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L6) — `void EnhancedRenderGraph::RequireQueueIdle() const`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L10) — `throw std::logic_error("Graph is retained by queue recordings; collect completed batches before mutation");`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L14) — `bool EnhancedRenderGraph::SubmitQueues(const std::shared_ptr<EnhancedRenderGraph>& self,`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Baseline: `ef829373c41138b8f47dd4011b076e1ec4d5829f`.

## Integrated source paths

- Opt-in meshlets/LOD are built before CEMC hashing/staging, and model generation
  publication remains atomic. (At review time v11 kept v9/v10 readers; `bf3fe4f3` removed
  them before merge, so only v11 is accepted.)
- DX12/Vulkan expose optional mesh pipelines, capability limits, generation-owned uploads,
  typed dispatch checks and completion-owned resource retirement
- Standard static opaque GBuffer uses GPU meshlet visibility, projected-error LOD and
  indirect mesh dispatch; other supported geometry retains indexed paths
- Native current-frame HZB has matching opaque donor topology, shared fragment discard,
  separate depth and a shared max pyramid. Recheck filters actual prior GPU-selected work
- LX consumes its earlier native depth version. Native shadow visibility uses independent
  per-cascade receiver cylinders and indexed indirect commands, never camera HZB

## Reviewed invariants and corrected findings

- Stream ownership, overflow/count limits, exact triangle corner/primitive mapping,
  geometry/settings digests, conservative spheres and disabled unsafe cone rejection
- Coarse indices reference base-used vertices; original index arrays remain unchanged
- Invalid optional runtime data has observable indexed fallback; staged new generations
  and cook export reject derived-data decode warnings
- Shader ID-indirection semantics belong to immutable shader generations, not layout IDs
- Current pose palettes, active bone ranges, zero-weight sentinels, double-precision
  bounds and conservative unknown-input handling
- Vertex/index/meshlet states, indirect barriers, optional Vulkan entry points, PSO kinds,
  compiler profiles, rollback and retirement
- Both production paths establish the upload-prefix boundary before recording-owned
  palette/visibility preparation, including CPU-only/custom fallback views
- Failures after donor depth recording abort rather than leave phantom occluders
- UI counters distinguish prepared/submitted work from unknown GPU-visible counts
- Editor controls use queued game-thread commands, native/UTF-8 paths and the same
  identity-preserving transaction as CLI authoring

## Deliberately unrun acceptance

No C++ build, shader compilation, tests, executable probes, renderer, captures, image
comparisons or GPU measurements were run. Source review is not proof of executable
correctness, visual parity, performance or hardware compatibility. Project XML/source
registrations and whitespace/diff checks were inspected only.

Future authorized execution should cover both backends, capability fallback, old CEMC,
malformed acceleration data, cuts/resize/multiple views, skin animation, masked coverage,
mixed indexed/mesh batches, LOD transitions, shadow cascades, upload/submission failure
and resource pressure.

## Explicit coverage limits

GPU LOD is limited to eligible mesh-shader assets/devices. Indexed fallback uses GPU
instance visibility with LOD0. Skinned mesh shading/LOD, arbitrary custom deformation,
ordered transparency, DXR and cone culling are outside this checkpoint. Malformed legacy
skin fit and extreme-coordinate meshoptimizer conditioning are not new correctness
promises. Extra prepass/compute/allocation costs and any benefit remain unmeasured.

## Material coverage extension

The initial checkpoint's transparency/custom direct-route limits are superseded by the
[material coverage extension](GpuDrivenMaterialCoverage.md). Accepted materials retain
original shaders/effects on GPU-produced indexed/nonindexed indirect paths. Generic custom
streams are preserved rather than compacted; CPU global ordering and submission remain.
Mesh-stage/LOD restrictions and hardware compatibility are separate from material routing.
The same no-build/no-test/no-runtime verification limit applies.

</details>
