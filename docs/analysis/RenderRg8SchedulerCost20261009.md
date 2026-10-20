# RG8 scheduler allocation cost — 2026-10-09

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/RenderTests/RHI/DX12/Tests/EnhancedFinalRg5Tests.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/RHI/DX12/Tests/EnhancedFinalRg5Tests.h#L30) — `camera.view = math::look_at_lh(camera.eyePosition, math::vector3{0.f, 0.f, 3.f}, math::vector3{0.f, 1.f, 0.f});`
- [Editor/RenderTests/RHI/DX12/Tests/EnhancedFinalRg5Tests.h:60](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/RHI/DX12/Tests/EnhancedFinalRg5Tests.h#L60) — `resources.AbortFrame();`
- [Editor/RenderTests/RHI/DX12/Tests/EnhancedFinalRg5Tests.h:143](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/RHI/DX12/Tests/EnhancedFinalRg5Tests.h#L143) — `Cleanup recording{[&] { resources.AbortFrame(); }};`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## Scope

Base: `d5a6a26a78da93686b8168429183ee7e0d50a8de`, with the local SSGI history-role fix and dirty-worktree changes recorded in the evidence patch. RG8 remains progress / default OFF. This is a scheduler CPU optimization, not GPU performance acceptance.

`BuildQueueDependencies` rebuilt predecessor/successor and per-resource reader vectors for every candidate assignment. Keep their capacities within a single `BuildQueueSchedule` call, clear all contents/state for each trial, and swap reader-epoch buffers instead of discarding their allocations. Candidate rules, dependencies, barriers, queue cost model, and pass-order selection are unchanged. No persistent cross-frame dependency cache was introduced.

## Release product measurements

Evidence: `Build/Verification/Phase43/RG8Scheduler20261009/normal` and `performance-summary.json`. Forward 0/1/2 and reverse 2/1/0 each ran in one process, 32 accepted chronological normal samples per segment (192 total). Measurement identity, complete history, schedule and calibrated intervals were audited. Timing excludes GPU validation and image readback, but retains evidence serialization. Clocks were not locked.

Async scheduling CPU median:

| Order | Previous history-fix build | Capacity-reuse build |
|---|---:|---:|
| Forward | 4.2045 ms | 0.8014 ms |
| Reverse | 3.8823 ms | 1.0575 ms |

Observed reduction: approximately 73–81%. This compares separate builds/runs, not randomized paired samples; it is not an FPS improvement claim.

GPU span median / p95:

| Order | Mode 0 | Mode 1 | Mode 2 |
|---|---:|---:|---:|
| Forward | 2.4438 / 2.5293 ms | 2.5697 / 2.8918 ms | 2.8206 / 3.4366 ms |
| Reverse | 2.5590 / 2.9798 ms | 2.8580 / 3.5891 ms | 2.8867 / 3.8113 ms |

All 64 mode-2 samples submitted compute, but measured overlap was zero. Planned barriers remained a median 719. GPU benefit is not established; async was slower than mode 0 in both orders in this run.

## Remaining scheduling problem

Inspection of the previous history-fix accepted compute frames (55) found Shadow ending before GBuffer in all 55, with SSAO placed on compute in only one. Most compute work was visibility reset/cull and occlusion HiZ. The list simulation prioritizes earliest start, then compute ancestors, then compiled position. A ready Shadow can therefore be consumed while GBuffer is waiting for visibility work, leaving little graphics work to overlap the later AO chain.

The measured batch intervals also show gaps between producer/consumer queues. The current handoff/minimum-gain model (20/50 microseconds) does not separately model all barrier, submission and contention costs. These gaps cannot be assigned entirely to GPU waits without further instrumentation. Do not remove actual producer/consumer dependencies to manufacture overlap.

Next GPU-facing work is to compare safe alternative placements that retain substantial graphics work alongside the compute chain, and reject plans whose measured execution cost outweighs their modeled gain. This note does not claim that change was implemented.

## Validation

Release build and RG8-specific native checks are recorded in the evidence directories. A full-regression diagnostic also exposed validation messages whose original error text was lost through function-argument evaluation order in `EnhancedFinalRg5Tests.h`; the drain and message construction are now sequential. Final validation results are appended below after verification.

The initial combined run failed because RG8 native-test cleanup called `AbortFrame` twice on the same closed primary list after its own validation drain. Its message reached the subsequent plan/final test. The planning fixture additionally requested an ignored ShaderResource initial state for a default-heap buffer. The fixture now creates COMMON (its imported read state is hypothetical for compile-only scheduling); native cleanup aborts only an active recording. Planning validation now checks its own debug messages without changing the 111 semantic-check count. An isolated GPU-validation plan run passed; the combined run after diagnostic draining also confirmed the full RenderGraph suite passes once that earlier message is attributed correctly. Neither result substitutes for a final combined run with the cleanup fixed.

Final Release validation (`native-final`): RG8 execution 428 checks, planning 111 checks, full RenderGraph regression including RG5 final 129 frames / 9 stages all passed; wrapper exit 0. GPU validation reported zero problems at the test gates. The existing profiler shutdown diagnostic `abandoned=1 retained=1 foreign=0 - [RHIThread]` remains in stderr; this is not a claim of a completely clean profiler shutdown. Build retains the existing Utility_Framework PDB LNK4020 warnings. Debug and Vulkan were not rebuilt or measured in this slice.

The timing run predates the final test-fixture-only rebuild; each collection records its own binary identity. Production scheduler code is unchanged between these runs. Build logs: `Build/rg8-scheduler-scratch.log` and `Build/rg8-scheduler-final.log`.

Final product captures (`captures`, `output-comparison.json`): six processes / 24 captures exited normally. Controlled inputs matched. 384 logical attachment/stage comparisons (previous history-fix mode 0 versus current mode 0, and current mode 0 versus modes 1/2) were finite and bit-identical, maximum absolute error 0. Shared readback attachments are counted by logical stage name, not independent GPU work. Capture runs did not enable GPU validation and are not performance samples.

</details>
