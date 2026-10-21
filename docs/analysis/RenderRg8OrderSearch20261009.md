# RG8 alternative queue-order search — 2026-10-09

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Base: `d5a6a26a78da93686b8168429183ee7e0d50a8de` plus recorded local changes. This follows the history-role and dependency-scratch fixes. RG8 remains default OFF / progress; no GPU adoption claim.

## Implementation

Every queue-assignment trial in overlap placement now compares two legal topological orders:

1. Existing earliest-start order, breaking ties in favor of compute ancestors and compiled position.
2. Compute-input-first order: prioritize compute work and its ancestors ahead of unrelated work, then earliest start and the existing tie breakers.

The second order can deliberately leave graphics idle while a compute prerequisite completes. It is selected only when the modeled whole-frame completion, including the existing cross-queue handoff and final join, is strictly smaller. Ties retain the existing order. The selected assignment is re-simulated with the same comparison to emit its actual order.

There are no pass-name, Shadow, SSAO, scene-name or fixed pass-index rules. Predecessor legality, physical state dependencies, recording side effects, compute compatibility, complete measurement requirement, declaration-order mode and single-queue fallback are preserved. Scope is a bounded two-heuristic search, not an exhaustive optimum or a beam search.

The existing 20 us handoff / 50 us minimum-gain model is unchanged. It does not yet separately price every added barrier or execution contention; a lower prediction alone is not performance acceptance. Native/product validation and actual queue intervals remain required.

## Regression

Added an anonymous seven-node graph, with a renamed reverse-declaration variant. Costs are analytical test inputs, not product measurements. The earliest-start-only local search reaches 2500 us in the forward fixture; the alternative order must find at most 2400 us (serial 2900 us), retain all producers before their consumers, and execute no callbacks during planning. Existing no-benefit, incompatible-state, side-effect, preserved-order and model-guard cases remain in the suite. Plan semantic checks increase from 111 to 119; both strict wrappers are updated together.

Evidence directory: `Build/Verification/Phase43/RG8OrderSearch20261009/`. Build log: `Build/rg8-order-search.log`. Runtime and performance results are appended after execution.

## Release results

VS 2026 Release build passed, with existing Utility_Framework PDB LNK4020 warnings. Native execution 428 checks, planning 119 checks and full RenderGraph regression passed under GPU validation (`native`, wrapper exit 0). No Debug/Vulkan claim. The existing profiler shutdown `abandoned=1 retained=1 foreign=0` diagnostic remains recorded separately from GPU validation.

The same-process forward 0/1/2 and reverse 2/1/0 timing matrix yielded all 192 required samples (32 per segment), with complete history. Independent audit of schedules and calibrated timing intervals passed. Clocks were not locked; timings exclude GPU validation and image readback, but include evidence serialization. Do not compare absolute GPU times across earlier builds as a controlled A/B experiment.

| Order | Mode 0 GPU median / p95 | Mode 1 | Mode 2 |
|---|---:|---:|---:|
| Forward | 3.2236 / 4.0878 ms | 3.2082 / 3.7868 ms | 3.6895 / 5.6075 ms |
| Reverse | 3.3751 / 4.0100 ms | 3.3382 / 3.8595 ms | 3.8057 / 4.8251 ms |

Mode 2 selected compute in 64/64 frames. Overlap above clock error occurred in 14/32 forward and 16/32 reverse frames. Median overlap was 0 / 0.1336 ms, maximum 0.2755 / 0.5448 ms. Intersecting pass intervals identify primarily `LX.Scene.RuntimeLookupMetadata` with `SSAO.Compute`; a few frames also intersect `SSGI.StoreHistory` with PostChain.Uber/FXAA. This is **not** evidence of Shadow/SSAO overlap.

Scheduling CPU median: 1.4033 / 1.1786 ms (previous capacity-reuse run 0.8014 / 1.0575 ms, separate-build comparison). Planned barrier count median rose from 719 in that run to 767; selected assignments also changed with measured costs. New search therefore has a measurable CPU cost and produces some real overlap, but the async GPU span remains slower than mode 0 in both orders. No performance adoption; default OFF remains.

Evidence: `performance-summary.json`, `overlap-detail.json`, `normal/` and binary metadata. The remaining limitation is whole-execution cost prediction and selecting useful independent graphics work, not lack of any alternative-order evaluation. Further work must account for submission/barrier overhead and reduce search cost rather than force more compute assignments.

Product output verification: all six capture processes / 24 captures exited normally. Controlled input signatures matched. 384 logical attachment/stage comparisons across previous mode 0 versus current mode 0 and current mode 0 versus modes 1/2 were finite and bit-identical (max absolute error 0). Capture data is correctness-only, with GPU validation disabled; GPU validation was exercised by the native suite. No commit or push was performed.

</details>
