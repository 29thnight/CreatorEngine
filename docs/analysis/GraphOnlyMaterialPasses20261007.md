# Graph-only material passes — 2026-10-07

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/ShaderMeta.h:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L24) — `enum class ShaderPropertyType : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:98](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L98) — `enum class ShaderPassQueue : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:106](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L106) — `enum class ShaderBlendMode : std::uint8_t`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/ShaderMeta.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L28) — `outError = std::string(context) + ": " + std::string(detail);`
- [Engine/RenderEngine/ShaderMeta.cpp:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L65) — `const bool known = std::any_of(allowed.begin(), allowed.end(),`
- [Engine/RenderEngine/ShaderMeta.cpp:102](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L102) — `else return Fail(context, std::string("'") + key + "'는 true|false여야 한다", outError);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## Product contract

Live scene material draws must carry an owning MaterialGraph source and the exact requested graph instance. Graph program preparation defers the frame; unsupported or failed compilation rejects it. Neither a native PBR material nor a previous submitted graph instance may replace that request. A previously completed display can remain visible while preparation runs; it is not a newly rendered fallback frame.

## Removed structures

- Live GBuffer/Forward ShaderMeta candidate application, native material sealing, accepted-generation owners and primary catalog loads.
- Graph-to-native fallback draw copies, their GBuffer/Shadow admission, and the capture/replay bridge recording those copies.
- SceneHost selection of previous submitted material/coverage and partial ready draw streams.
- Native material pipeline initialization and native geometry visibility recording in live Shadow/GBuffer. These owners now prepare cascades or initialize neutral targets through explicit graph-target APIs.
- Native Forward material pipeline initialization in the live node. Forward+ retains its shared light-list computation and records graph materials.

## Pass audit

| Pass family | Material routing after this change |
| --- | --- |
| Shadow | Cascade fitting and neutral depth initialization; MaterialGraph alone records material geometry/coverage. |
| GBuffer | Neutral attachment initialization; MaterialGraph alone records scene material geometry/coverage. |
| Opaque color, transmission/refraction, subsurface, volume | SceneHost consumes the exact graph input selected for this frame. |
| Forward+ blended surfaces | Shared light lists and graph draw ordering remain; no native material fallback is admitted. |
| Decal, Sprite, UI | Dedicated prepared resources and shaders; no SceneHost-to-native material fallback. |
| IBL, Deferred, SSAO, SSGI, SSR, Fog, SkyBox, PostChain, Editor overlays | Dedicated algorithms/resource consumers; not alternative scene-material routes. |

Low-level native pass APIs remain as explicitly invoked standalone regression/reference implementations. They are not initialized or selected as a live material fallback. Indexed geometry capability paths, optional neutral textures, disabled effect pass-through and resource allocation policies are independent from material substitution.

## CSM cache consequence

The live depth map no longer combines native and graph material casters. It still has a clear writer followed by the graph depth writer. A future cache hit must skip both initialization and graph depth recording, retain exact graph dependencies and publish only after successful submission. PR #116's submission, multi-view and retirement requirements still apply. No depth-content cache is introduced here.

## Validation

Evidence is collected under `Build/Verification/GraphOnlyMaterials20261007/`. Acceptance requires current Debug/Release builds, native graph-only depth initialization without native material PSOs, exact generation selection/rejection tests, relevant material pass regression and live capture. Earlier RG6 acceptance archives describe their own source hashes and do not prove this later change.

Completed checks:

- Debug/Release RenderEngine, MaterialRasterSurfaceProbe and CreatorEditor builds passed. Release MaterialVulkanSceneProbe also built with the repository's Vulkan-Headers include path supplied to the build environment.
- Debug and Release DX12 scene integration passed independently: 59 frames each, five exact generation submissions, seven rejected pending/failed requests, zero material fallbacks, one stale publication rejection and one aborted recording. Neutral GBuffer depth and all three shadow layers were read back at 1.0 without native material PSO initialization.
- Release complete raster regression passed: 59 composition frames, 330 rejected resource/declaration candidates, 24 shared-depth frames, six skinned-depth frames, five exact generation frames and seven pending/failed material rejections. All five updated wrapper signatures match the actual success marker.
- Release shadow/decal passed: 45 shadow frames, 114 decal frames, 72 rejected decal candidates and GPU validation zero. CSM math contracts and 384-instance/192-batch partition equivalence passed with 0/2/4/5/6/8 workers.
- Release special materials passed with GPU validation zero: refraction 24 frames (48 rejected candidates, roughness/TIR/miss/masked/hybrid checks), subsurface 12 frames (spread/boundary/mask checks), volume 33 frames (66 rejected candidates and transport/scattering/interior checks).
- Release product BASE-0 passed again after the final contract comment/fixture repairs: two independent Editor processes, four accepted captures, normal exits, GPU validation zero and no source drift. All accepted draw routes are `lattice`; each compiled graph contains `Shadow.Clear` and `GBuffer.Clear` and contains neither native `Shadow` nor native `GBuffer` material pass. See `Live-Release-final/result.json`, `final-live-graph-only-routing.json` and `final-source-hashes.json`. The earlier `Live-Release` run is retained separately.

Release Vulkan build passed, but runtime acceptance did not: initial asynchronous material preparation exited 1 before the first scene fixture. `WaitSceneProgram` has a 600-second SPIR-V deadline; the diagnostic contains no shader/driver detail. No Vulkan pixel/validation success is claimed. Cold preparation diagnosis and actual resource/pixel acceptance are handed to [PHASE 4.9](../plans/BackendParityPlan.md), without restoring material fallback or relaxing the timeout to declare success.

The earlier scene/raster checks precede the final comment/test-only repairs; engine execution logic did not change afterward. Final Debug/Release engine, probe and Editor builds passed; the final Release product capture verifies the final production source hashes. Scoped format checks passed. No commit or push is part of this change. This is not a new Debug/Release RG6 performance comparison or a CSM depth-content cache acceptance.

Excluded diagnostic attempts: the first depth-clear check copied only array slice zero; the corrected test explicitly copies each layer. Earlier generation rejection checks used aggregate resource deferral and obscured a shader failure during exception cleanup; selection-stage deferral is now checked separately. The CSM math fixture omitted its required device service and its rejected-cascade assertion confused submitted indirect commands with GPU-visible draws. The corrected fixture requires three submitted GPU candidates (two direct commands on devices without indirect support), checks per-cascade candidate counts, and still requires the rejected cascade's depth pixels to remain neutral. Failed/hung attempts are retained and excluded from acceptance. Exceptional fixture cleanup remains a separate lifetime issue; successful runs explicitly shut down their resource listeners after GPU idle.

</details>
