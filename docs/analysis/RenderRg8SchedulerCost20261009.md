# RG8 scheduler allocation cost — 2026-10-09

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
