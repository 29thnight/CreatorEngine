# RG8 alternative queue-order search — 2026-10-09

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
