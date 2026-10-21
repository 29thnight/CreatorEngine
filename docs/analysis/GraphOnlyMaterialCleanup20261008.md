# Graph-only material cleanup — 2026-10-08

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/Contract.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Contract.h#L1) — `#pragma once`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Base: `9c1c2aeae0e85cc3a2caacea142aee1e706cf88f`. This cleanup preserves the exact requested MaterialGraph instance, all-or-nothing readiness selection, graph transparent ordering and recording-owned resource lifetime.

## Changes

- `SceneHost::SelectReadyInput` checks every requested program before copying the input. Ready inputs copy draw records once and update only `selectionRevision`; pending or failed inputs do not allocate the selected input. Geometry payload owners remain shared.
- Removed the unpopulated native `opaqueShadowIndices` loop and the live native `forwardDraws` sort. Graph shadow eligibility and Forward+ graph ordering remain. Empty native input vectors still serve the existing common pass and standalone capture/replay interfaces; this is not removal of standalone native regression APIs.
- Removed `PooledDraw::authoredMaterialSource` and `authoredRevision`, including their mesh and foliage assignments. Graph source and model generation owners remain.
- DX12 and Vulkan now share `PrepareSceneRecording`: residency → recording boundary → palette upload → Decal/Sprite visibility → graph material preparation → optional capture input. Shader output scope, failure propagation and material preparation deferral are preserved.
- Updated stale comments about native material sealing and cold-compile fallback.

## Scope and validation

Other dirty worktree changes are outside this cleanup. No material binding cache was introduced. The existing texture/sampler caches, per-draw data and descriptor validity rules remain unchanged. No FPS or CPU performance improvement is claimed without a new equivalent workload measurement.

VS 2026 / VS18 / v145 Debug and Release RenderEngine + MaterialRasterSurfaceProbe builds passed. Both complete DX12 raster regression processes exited 0 with GPU validation enabled and the expected success marker. Each reported:

| Contract | Result per configuration |
| --- | --- |
| Scene composition | 59 frames, 330 rejected candidates |
| Exact generation selection | 5 frames, 0 fallbacks, 7 rejected pending/failed requests |
| Recording/publication rejection | 1 abort, 1 pending submission, 1 stale publication |
| Shared/skinned depth | 24 / 6 frames |
| Scene input rejection | 24 frames, 24 failures checked |
| Complete raster checks | 21,072,207 checks, 3,879,955 GPU components |

Scoped removal/caller checks and `git diff --check` passed. SHA-256 hashes of the two modified sources were unchanged throughout the accepted runs. Both backend helper instantiations compiled; this is DX12 native regression acceptance, not a fresh live Editor capture or Vulkan runtime acceptance.

Artifacts use `Build/Verification/graph-only-cleanup-*`, including separate Debug/Release build/runtime logs, source hashes, static checks and `acceptance.json`. The raster wrapper's first build collided with another Editor build, and its subsequent runtime used an outdated dependency DLL path. Neither attempt counts as runtime acceptance. Accepted runtime commands use the actual `vcpkg_installed/x64-windows/x64-windows/{debug/bin,bin}` paths. Release linking retained LNK4020 warnings about Utility_Framework PDB type records; runtime checks still passed. These checks do not measure or claim a performance improvement.

## Subsequent issue integration

The initial acceptance above predates the complete #131 removal and the #133 binding cache. The subsequent all-issue implementation replaces mutable live native streams with one const empty reference boundary, removes PooledDraw material/transparent metadata, and admits shadows only after graph-source validation. The latest Debug full raster integration passed the same 21,072,207 checks and 3,879,955 GPU components; Editor graph WireFrame static/skinned transport also passed. Current broader lifecycle and backend validation is recorded in GitHubIssueRemediation20261008.md. The original source hashes validate only the original cleanup slice.

Latest Release full integration also passed 21,072,207 checks / 3,879,955 GPU components after lifecycle integration. No equivalent-work performance acceptance is inferred from these correctness runs.

</details>
