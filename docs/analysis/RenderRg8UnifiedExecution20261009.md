# RG8 unified execution: implementation and validation boundary

Base: `cd3fa2e2f8cf40d4c810731430f8be4503a09f1e`.

This change is statically reviewed implementation with authored, unexecuted acceptance coverage. No build, compiler, shader compiler, native test, GPU probe, benchmark or repository regression script was run while preparing it. It does not establish a performance improvement, runtime correctness, or RG8 adoption.

## Access and state planning

The queue dependency graph no longer chains every physical use unconditionally. It groups immutable reads only when their state and assigned queue agree. Each state/ownership change or writer joins the complete preceding reader frontier. Version-derived RAW, WAR and WAW edges remain authoritative. Every candidate placement rebuilds its queue-specific state dependencies before simulation, and the selected order is replanned for actual barriers and resource-use lifetimes before recording.

This is a whole-resource contract. Mips, array slices and buffer regions are not independently schedulable until the declaration API can describe them. Repeated passes do not join an immutable-reader group because their internal phases can change state; all phase states are also checked before compute placement.

Cross-queue reads remain conservative. DX12's graphics `ShaderResource` mapping contains PIXEL and NON_PIXEL, while its compute mapping contains only NON_PIXEL. Equal RHI enum values are not proof of a legal shared state. The planner does not union those flags or let a compute list transition a graphics-only state. See the [DirectX legacy-state restriction](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#compute-queues-and-d3d12_resource_state_pixel_shader_resource).

Same-graphics-queue scene-input reads can now reorder, allowing a graphics producer to feed compute while another independent graphics reader runs afterward. The Shadow/GBuffer/AO/Deferred shape is a regression fixture, not a product scheduling rule. The planner does not recognize their names or prescribe their order.

Queue boundaries retain the existing COMMON release/acquire contract and a graphics epilogue joins the compute tail. All recording targets retain graph/storage leases through their actual queue completion. Frame retirement still protects imported uploads, descriptor storage, allocator reuse and transient return. RG7 aliasing stays disabled for multi-queue execution; its single-total-order lifetime proof is not silently reused for concurrent queues.

`hasSideEffect` remains a culling-root flag. New `DeclareRecordingSideEffect` explicitly fences owner-thread CPU effects against all live preceding/following callbacks; those graphs use unsplit owner recording. Synthetic diagnostic edges have reason 3 and invalid-resource sentinel 65535.

## Recording and submission

Owned queue execution now uses the same engine job dispatch contract as RG4. Owner-opened independent command targets are filled with fresh per-slice encoder state, first/last slice barriers, contiguous submission order and dependency recording waves. Small record-cost workloads retain the single-target path. Explicit CPU-effect callbacks remain on the owner.

The existing primary graphics queue is exposed through a retained endpoint with its own retirement timeline. Prefix/suffix ordering remains on the RHI submission owner. Same-native-queue waits are unnecessary; inter-queue waits and final frame fences remain. One CPU submission dispatch admits the graph's queue operations instead of synchronously dispatching each wait/submit separately. This is CPU admission, not a GPU-completion wait.

## Measurement and capture

- Normal and PBR-capture graphs have separate timing history, failure watermarks and bounded eviction
- Each in-flight submission retains its domain, requested mode and its own compute-profiler token for delayed resolve/collection
- Measured zero is valid; unknown, partial, stale or saturated costs cannot fabricate a positive benefit
- Fallbacks distinguish disabled, unsupported, missing measurement, unsupported state and insufficient gain
- Capture reports include requested/effective mode, measurement domain and fallback/counter provenance
- Identical versioned HDR stage sources share one readback; every original stage name still refers to the correct copied image

PBR capture domain is independent of profiler trace-capture admission (`captureGeneration`). A trace capture can contain a normal graph, and PBR readbacks can exist without a trace generation. Capture images and validation records are correctness evidence, not normal-frame timing samples.

Short graphics prologue/epilogue timing scopes include graph boundary work and the joined compute tail in GPU span. They have invalid pass identity and do not feed scheduling costs. They are not an enclosing graphics interval, which would incorrectly count queue waits as executed overlapping work. The GPU span still excludes host presentation and scan-out.

With evidence telemetry enabled, `renderOnceMilliseconds` covers one view's resource preparation, graph declaration/compile, recording, scheduling and submission through frame-retirement enqueue. `compileMilliseconds` and the existing narrower record/submit fields remain separate. The optional full-view timer is not started in ordinary evidence-disabled frames. This scope excludes main-thread scene generation, presentation and GPU-completion waits.

## Supported compute declarations

Dispatch-only HiZ construction, versioned geometry/meshlet visibility reset/cull, SSGI dispatch stages and compute post-processing now declare compatibility. Their real resource dependencies still determine placement. SSGI history copies, legacy RG1 visibility and graphics callbacks remain outside this subset. No depth prepass or currently disabled live HZB path was forced on. Existing meshlet changes require fresh normal-frame costs; older timing signatures do not certify their benefit.

## Same-process measurement and authored acceptance

The existing command service exposes `render.queue.mode 0|1|2`. It requests mode application on the render owner before the next frame begins and acknowledges a successfully submitted frame by request ID. It does not rebuild shaders/PSOs, clear normal timing history, drain the GPU, or claim effective compute placement. A newer request supersedes an older one. Already submitted frames retain their original profiler/measurement provenance.

The normal timing harness switches OFF/owned-graphics/overlap in forward and reverse order in one process, warms each segment, and joins mode/domain/frame/view/submission evidence. Diagnostic captures and GPU validation use separate collection. The strict audit compares CPU and GPU p50/p95, requires complete normal measurements, tests actual calibrated overlap above clock uncertainty and checks a negative-fallback workload. Prediction and batch counts are not overlap or speedup evidence.

Authored native coverage includes shared graphics reads, all-reader writer joins, incompatible states, missing/zero/saturated costs, recording-side-effect ordering, distinct capture histories, worker counts 1/2/4, split-output equivalence, small-cost fallback, recording exceptions, retained frames and actual interval reporting. The exact callback-order fixture deliberately uses one recording worker; parallel fixtures use disjoint/atomic CPU bookkeeping.

## Remaining acceptance work

1. Build the exact PR commit and run the authored static/native regression workflows on a supported DX12 device
2. Run output equivalence and synchronization/lifetime checks with GPU validation, separately from performance sampling
3. Collect same-process warmed normal-frame A/B data including full-view CPU cost and complete graph GPU span
4. Establish both an actual-overlap workload and an insufficient-benefit fallback workload on the target hardware
5. Inspect regressions after new meshlet timing, contention, recording and submission costs; retain the serial mode when benefit is not established

The analytical cost model remains uncalibrated. General simultaneous cross-queue read epochs, subresource-level scheduling and multi-queue transient aliasing are deliberately not claimed by this legacy-state implementation.
