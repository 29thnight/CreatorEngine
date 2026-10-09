# RG7 transient buffers and placed heap sharing — 2026-10-08

RG7 is closed for bounded DX12 implementation/correctness/measurement with default OFF. See [final closure and adoption decision](RenderRg7Closure20261008.md). Sections below retain chronological evidence and historical pending statements; the final closure supersedes them. Q0, RG8 and RG9 remain pending.

## Implemented scope

- Graph-owned transient buffers have a declared kind before native allocation and participate in versioned dependency/culling/lifetime/barrier planning. Reset/destruction after GPU completion releases uncached resources or returns cached placed-resource groups. Imported buffers remain borrowed.
- Backend-neutral placed allocation queries and a graph-retained heap owner keep native heap handles out of RenderGraph. Unsupported backends retain committed resource allocation.
- DX12 uses actual GetResourceAllocationInfo size/alignment, homogeneous buffer/non-RT/RT-DS heaps compatible with heap tier 1, and CreatePlacedResource at offset zero. Greedy groups reuse a heap only when compiled lifetimes do not overlap. Single-member groups stay on the existing committed/pool path. Supported sharing requires explicit versioned write declarations and an active owner recording; inferred legacy UAV access is rejected.
- Aliasing activation precedes transitions and every RT/DS activation is fully cleared with its optimized clear value, including array slices. Repeated-pass first uses and first reads/read-write uses remain outside heap sharing. Imported/history resources are excluded.
- Aliased textures never enter the committed texture pool. Completed placed-resource groups can return to the bounded group cache; uncached/evicted groups release placed resources before heaps. The caller must retain the graph through submission completion as before. Recompiling a graph with placed resources requires Reset after GPU completion.
- The DX12 live path can opt in with CREATOR_RENDERGRAPH_ALIASING=1. CREATOR_RENDERGRAPH_EXTEND_LIFETIMES=1 prevents sharing by extending lifetimes through the final pass. The default remains the existing committed/pool path until product acceptance.
- Diagnostic resource snapshots expose alias group and native allocation bytes; when sharing is enabled, graph stats report allocated bytes with/without sharing and reuse count. The disabled default avoids allocation queries and reports no allocation metrics. Tests query the committed baseline independently. These are actual heap allocation sizes, not a claim about process-wide resident VRAM or pooled free memory.
- Activation barriers are explicitly marked in diagnostic snapshots, capture manifests, the Editor command reader and graph viewer. Extended-lifetime testing extends allocation occupancy only; it does not rewrite the actual authored resource-use interval.

Initialization and activation follow [CreatePlacedResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createplacedresource).

## Validation

Tools/regression/verify-rg7-transients.ps1 invokes `dx12.rendergraph transient` in the existing Editor with GPU validation. Five modes compare committed, sharing, forced extended lifetimes, an overlapping-live-resource guard and two-command-target parallel recording. Fixtures include color/depth arrays and buffer payloads, culled unused buffers, allocation-byte accounting, invalid used buffer rejection, inferred-access rejection, imported buffer retention and post-Reset release. `-IncludeFullRegression` also runs the existing full RenderGraph test. The harness records executable/runtime hashes and uses fresh workspace state with settling frames before/after diagnostics.

VS 2026/v18/v145 Debug and Release builds passed with the inferred-access guard and diagnostic activation export. Both configurations passed the existing full RenderGraph regression and five-mode GPU fixtures, with inferred UAV access and invalid used buffers rejected, imported ownership retained, validation messages zero and process exit zero. The final suite also rejects invalid heap allocation, oversized placement, non-COMMON initialization, texture/buffer heap-class mismatch, foreign heap implementations and recompilation without Reset, and checks diagnostic barrier counts against executed-plan statistics. Evidence: `Build/Verification/Phase43/rg7-{debug,release}-diagnostics-build.log`, `rg7-{debug,release}-diagnostics-acceptance.log` and `RG7/{Debug,Release}/binary-metadata.json`. The implementation has not yet earned the full RG7 completion gate.

| Fixture mode | Shared heap reuse | Allocated bytes |
|---|---:|---:|
| Committed baseline | 0 | 393216 |
| Aliasing | 4 | 131072 |
| Extended lifetimes | 0 | 393216 |
| Overlap guard | 3 | 196608 |
| Parallel recording (2 targets) | 4 | 131072 |

Each mode compared every authored color/depth pixel and all buffer elements against the committed baseline. These fixtures show a 256 KiB allocation reduction (66.7%) for the sharing case; they do not establish product VRAM savings or CPU/GPU speedup.

The first parallel Debug run passed GPU comparisons but exited with code 3 after an ImGui initial-docking-size assertion during early shutdown. This failed run remains in rg7-debug-parallel-test.log. The harness was corrected to use fresh workspace state and settle before/after the test; subsequent Debug/Release five-mode and full-regression runs exited normally. The underlying UI assertion was not changed by RG7.

The first Release live OFF/ON run used LX_CookFixture through the existing Editor BASE-0 harness. OFF passed two independent processes. ON produced two captures with zero DX12 validation messages, but artifact acceptance correctly failed: 104 executed barriers versus 94 exported barriers. Ten heap activations were missing from the diagnostic snapshot. Evidence is preserved at `Build/Verification/Phase43/RG7LiveRelease-20261008-155024`. The diagnostic/export implementation and a fixture snapshot/statistics assertion fixed this. The artifact auditor now checks shared-group lifetime exclusion and exactly one explicit-write first-use activation per member, while excluding allocation metadata from logical graph identity. The failed run is not accepted.

## Corrected Release live slice

`Tools/regression/verify-rg7-live.ps1` passed on the final diagnostic source using the same executable/runtime, static LX_CookFixture, 64×64 extent and one graph material draw. Both OFF and ON passed two independent Editor processes, two captures per process, source immutability, zero validation messages and exit zero. Sixteen attachments compared with max error zero. Ten placed resource activations in four shared groups were present and counted in the manifest. Imported resources stayed outside sharing. This is a small static Editor scene, not Scene/Game/preview or production-resolution acceptance.

Equivalent graph transient allocation queries totalled 51,904,512 bytes without sharing and 51,511,296 bytes with sharing: a 393,216-byte reduction (384 KiB, about 0.76%). These values include all used graph-owned transients; the six reused 64-KiB allocations produce this saving. Device budget snapshots are explicitly labelled `device-budget-snapshot-not-transient-peak`; they do not establish reduced peak committed/resident VRAM.

Fifty unique live timing samples per mode (25 per independent process, GPU validation enabled) gave:

| Mode | CPU record median / p95 (ms) | GPU median / p95 (ms) |
|---|---:|---:|
| OFF | 2.041 / 3.138 | 1.01912 / 1.55936 |
| ON | 2.252 / 3.447 | 1.10669 / 2.70586 |

This probe shows increased record/GPU costs, not a speedup. The small memory saving does not justify default activation. Keep sharing opt-in; the next RG7 work must establish representative resolutions/workloads and reduce heap/resource/allocation-query preparation cost before the adoption gate. The samples do not isolate a causal per-operation cost or prove a hard regression bound.

Evidence: `Build/Verification/Phase43/RG7LiveRelease-Diagnostics-20261008-1615/{result.json,timing-summary.json,off-on-comparison/comparison.json,Off/result.json,On/result.json}` and `rg7-release-live-diagnostics.log`.

## Completed-group cache slice

This implementation caches native allocation descriptions and completed placed-resource groups in the existing transient pool. Matching includes allocation class/size/alignment and ordered resource descriptions, including optimized clear colors. Each borrowed group retains its heap and resources until the caller completes GPU work and resets the graph. Reuse starts from the native resource's previous committed state; an abandoned recording preserves the original state. Free groups are bounded to 32 groups / 128 MiB, metadata to 256 descriptions. Device teardown clears groups before the native service shuts down. Captures expose native query, heap creation/reuse and resource reuse counters.

VS 2026 Debug/Release builds and the existing Editor full RenderGraph regression plus nine-mode GPU suite passed, with zero validation messages and exit zero. New modes verify cold creation, warm native-handle reuse, zero-capacity eviction, recreation with cached allocation metadata and complete cache drain. All color/depth pixels and buffer elements still match the committed baseline. Warm modes create no native heap and issue no allocation query; the buffer handle remains identical until eviction. Evidence: `rg7-{debug,release}-cache-build.log`, `rg7-{debug,release}-cache-acceptance.log`, and `RG7/{Debug,Release}/binary-metadata.json` under `Build/Verification/Phase43`.

| Cache mode | Heap creates | Heap reuses | Resource reuses | Allocation queries |
|---|---:|---:|---:|---:|
| Cold (5) | 2 | 0 | 0 | 3 |
| Warm (6) | 0 | 2 | 6 | 0 |
| Warm then evict (7) | 0 | 2 | 6 | 0 |
| Recreate after eviction (8) | 2 | 0 | 0 | 0 |

The fresh Release live OFF/ON run passed with the same static 64×64 LX fixture, same executable/runtime and two independent Editor processes per mode. Both modes have source changes zero, validation messages zero and process exit zero. Sixteen outputs have max error zero. All four warm ON captures reuse four heaps / ten resources, create zero heaps and issue zero allocation queries. Equivalent graph allocation remains 51,904,512 → 51,511,296 bytes (384 KiB saving); the cache is not additional in-frame heap sharing or proof of a peak resident VRAM reduction.

| Cache source mode | Samples | CPU record median / p95 (ms) | GPU median / p95 (ms) |
|---|---:|---:|---:|
| OFF | 50 | 2.5795 / 3.175 | 1.506275 / 2.00954 |
| ON | 50 | 2.163 / 2.886 | 0.977248 / 1.3088 |

The current same-binary OFF/ON probe has lower observed CPU/GPU timings for ON. It does not isolate the cache change against the older binary, establish a hard performance bound, or represent production-resolution Scene/Game/preview workloads. GPU validation is enabled and runs execute sequentially; keep default OFF until broader acceptance. The earlier diagnostic implementation's timings remain historical evidence and are not a controlled cache-before/cache-after comparison.

Per-process CPU medians are OFF 2.655 / 2.079 ms and ON 2.163 / 2.163 ms; GPU medians are OFF 1.07693 / 1.82858 ms and ON 0.937664 / 0.998784 ms. This run-to-run variation is a further reason to avoid treating aggregate medians as a stable product speedup.

Evidence: `Build/Verification/Phase43/RG7LiveRelease-Cache-20261008-1705/{result.json,timing-summary.json,off-on-comparison/comparison.json,Off/result.json,On/result.json}` and `rg7-release-live-cache.log`. The result retains `phaseComplete=false`.

## Poison and failure-recovery slice

`SetPlacedTransientPoison` is an opt-in graph diagnostic for shared RT/DS targets and float-view-compatible UAV buffers. RT/DS optimized clear values match the poison initialization (magenta / depth 0.125); buffers use NaN with a UAV ordering barrier before the producer. Other placed kinds are rejected rather than silently skipped. The mode does not poison committed/single-member resources and is not exposed as a complete live product diagnostic yet. Fixture modes 9/10 deliberately omit RT/DS writes and overwrite only half a buffer, using sequential and parallel recording respectively. GPU readback verifies every pixel in both array slices, the overwritten 32 buffer elements and the NaN tail. Barrier diagnostics/statistics include the poison ordering barriers, and placed cache identity includes optimized depth clear values as well as color values.

Failure fixtures use an isolated DX12 service override to inject allocation-query, second-heap and partial placed-resource creation failures while retaining actual native successful allocations. They invalidate a handle before activation, and reject an actual queue admission by temporarily unregistering only the isolated test device. Each case checks resource/heap release and a successfully submitted recovery frame. Warm-cache admission rejection checks that previous committed states survive into the recovery frame. This is not actual OOM, post-enqueue Signal failure or device loss. Sequential graph execution now catches activation and final-barrier exceptions through the same error return as pass-body failures.

VS 2026/v18 Debug and Release builds passed. In the existing Editor, both configurations passed the full RenderGraph regression, eleven-mode GPU suite and all six failure scenarios; validation messages are zero and processes exit zero. No separate test project was introduced. These correctness tests do not refresh the earlier live performance measurements.

| Failure scenario | Verified result |
|---|---|
| Second allocation query | Compile fails; no retained native allocations; recovery submits |
| Second heap creation | Earlier completed group releases; recovery submits |
| Second placement | Partially created local group releases resources before heap; recovery submits |
| Fourth placement | Earlier graph group and partial later group both release; recovery submits |
| Stale activation handle | Execute returns the activation error; abort/reset releases remaining resources; recovery submits |
| Queue admission rejected | Prior committed cache states remain CopySource; next frame reuses and submits without validation errors |

Evidence: `Build/Verification/Phase43/rg7-{debug,release}-poison-failure-{build,acceptance}.log` and `RG7/{Debug,Release}/{results.jsonl,binary-metadata.json}`. `RG7_TRANSIENT_OK modes=11` and six `RG7_FAILURE_OK` markers occur in each configuration. RG7 remains in progress with earned effort zero.

## Delayed-GPU lifetime and post-native failure slice

The submission coordinator now retains failed native submissions until an explicit GPU-idle/device-loss lifecycle boundary; observing their reserved completion value alone cannot release the graph or return its placed groups. CPU ticket completion is published after quarantine and GPU lifetime registration, so a completed ticket also observes those records.

The existing Editor fixture uses an actual DX12 queue wait on a separately signaled fence. Three frame slots retain three independently recorded graphs while GPU execution is blocked. Their native handles remain distinct, no placed group enters the free cache, and all three retirement tokens remain present. After opening the gate and draining, each group returns exactly once, late-reader committed resources release, and the imported history buffer remains caller-owned. Every element of the twelve buffer readbacks matches its expected view value or the shared history value. This tests serialized GPU execution across independent graph identities; it does not replace Scene/Game/Material Preview product acceptance or cross-queue handoffs.

A second fixture really executes and signals one native batch, then injects a failure return in the command-pool service. Even after its native completion fence is observed and the retirement poll runs, its graph remains retained and the owner stays blocked. Only an explicit verified GPU-idle drain releases the graph. This exercises the post-enqueue failure bookkeeping, but is not an actual native Signal HRESULT failure, OOM or device loss; the blocked owner is shut down rather than declared reusable.

The first Debug run passed the three-frame case and functional failed-submit checks, but final validation detected a duplicate Close in fixture cleanup after AbortFrame. Cleanup now aborts only an active recording. The failed evidence remains under `Build/Verification/Phase43/RG7Lifetime-Failed/Debug` and `rg7-debug-lifetime-acceptance.log`.

VS 2026/v18 Debug and Release builds passed. Both existing Editor runs passed the full RenderGraph regression, eleven GPU modes, six creation/admission failure cases and two lifetime modes, with validation zero and process exit zero. Final evidence is recorded in `Build/Verification/Phase43/rg7-{debug,release}-lifetime-repair-{build,acceptance}.log` and `RG7Lifetime/{Debug,Release}/{results.jsonl,binary-metadata.json}`. No separate test project or new live performance measurement is introduced by this slice. RG7 stays in progress, earned effort zero and aliasing default OFF.

## Native device-loss slice

The existing Editor suite now includes an isolated WARP adapter selected through the existing LUID initialization API. The fixture checks that WARP differs from the preferred Editor adapter before removing a device, because DX12 device creation can share a device for the same adapter. No new test project or production backend selection is introduced.

Two placed UAV buffers share a heap. The RHI thread queues their recorded commands behind a native fence gate, calls `ID3D12Device5::RemoveDevice`, and invokes the production native Signal path. The test checks the actual removal reason, CPU failure outcome, blocked/faulted owner, retained graph token and empty free-group cache. A device-loss fence sentinel must report zero through the completion API. Explicit unrecoverable-device abandonment then releases the graph, and clearing its cache releases all tracked resources and heaps. No readback correctness or completed GPU execution is claimed after removal.

The first Debug/Release full-regression runs failed the new fixture: Signal returned success after removal, so `SubmitCommandLists` reported a successful CPU submission without marking device loss. The production parallel submission path now checks the native `UINT64_MAX` fence sentinel even when Signal succeeds, obtains the removal reason and rejects/quarantines the batch. This is a removal-associated completion-state fix, not evidence that Signal itself returned a failing HRESULT. Original failed artifacts remain under `Build/Verification/Phase43/RG7DeviceLoss-Failed/{Debug,Release}` and `rg7-{debug,release}-device-loss-acceptance.log`.

The forced removal API and fence sentinel behavior are documented by [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice). This fixture exercises deliberate native API removal of a software device; it does not establish spontaneous hardware TDR recovery, actual OOM, or a healthy-device Signal failure. Build and runtime acceptance for this slice are recorded separately from the preceding lifetime slice.

VS 2026/v18 Debug and Release repair builds and final existing-Editor full regressions passed. Both runs include eleven GPU modes, six failure scenarios, two lifetime modes and the native WARP removal case, with zero validation messages and exit zero. Evidence: `Build/Verification/Phase43/rg7-{debug,release}-device-loss-repair-build.log`, `rg7-{debug,release}-device-loss-final-acceptance.log`, and `RG7DeviceLoss/{Debug,Release}/{results.jsonl,binary-metadata.json,stdout.log,stderr.log}`. The earlier Debug repair slice without full regression is preserved separately as `RG7DeviceLoss-DebugSlice`. These are correctness checks, not fresh product performance or default-adoption evidence. RG7 remains in progress with earned effort zero and aliasing default OFF.

## Product view/scene/resize slice

`Tools/regression/verify-rg7-product-views.ps1` runs the existing Editor RG-V product harness with aliasing ON/OFF in Debug and Release, fresh workspaces and copied fixture assets. It reuses native fixture evidence only for the same executable/runtime hashes. `audit-rg7-product-views.ps1` checks independent Scene/Game/Material Preview graph identities, actual scene/game shared-heap activation, imported/history exclusion, non-overlapping compiled group lifetimes, scene epoch replacement and representative extent resize. Controlled scene attachments are compared with matching camera/material/mesh inputs. No separate test project is introduced.

The first Release ON run reached Scene/Game graphs at 1498×730 with fifteen shared members each, but failed Material Preview readiness: requested revision 2 remained at completed revision 1. The visible Inspector preview took precedence over an explicitly pinned node-editor preview in `CapturePreviewRequest`, continuously selecting the Inspector revision. The node-editor pinned request now owns the shared preview target until unpinned; the Inspector remains the fallback when no active preview is pinned. Failed evidence is preserved at `Build/Verification/Phase43/RG7ProductViews-20261008/Release-On` and `rg7-release-product-on.log`. This failed attempt is not accepted.

This slice measures view/scene/resize correctness and controlled pixels. Device budget snapshots and per-graph allocation bytes do not establish peak committed/resident VRAM savings, and RenderPass UI timings are not alias preparation/frame performance acceptance.

The preview repair binary completed Release ON/OFF product runs, but the Debug live startup hit ImGui's zero-size docking ancestor assertion before its first HTTP response. That matrix is preserved at `Build/Verification/Phase43/RG7ProductViews-Repair-20261008` and is not final acceptance. The main layout root had been created without `ImGuiDockNodeFlags_DockSpace` and serialized as a floating `DockNode`. The builder now creates a proper dockspace; workspace restore promotes only the engine's exact main root identity before ImGui attaches children, preserving other floating groups and the existing split tree. `RG7DockLegacyReplay` replays the failing Debug workspace through 120 frames and normal exit. Its corrected structural audit preserves child IDs, parents, SizeRef and split axes; ordinary Selected/NoTabBar UI updates are excluded. The first literal-line audit failed on those normal UI updates, not an Editor crash. This is a targeted startup/layout fix, not a rewrite of workspace ownership.

VS 2026/v18 final Debug/Release builds and four fresh product Editor runs (ON/OFF per configuration) passed after both UI fixes. Each run observes independent Scene/Game/Material Preview views at 1498×730; ON has fifteen shared members and fifteen activations in every view, while OFF has none. Imported/history resources never join shared groups and each compiled group has non-overlapping ordinal lifetimes. Scene epoch changes 4→5 and extent changes 2246×1094→1124×548 in all four runs. GPU validation problems/dropped messages and process exit codes are zero. Same-binary controlled scene captures consume five graph draws including four robot meshes; seven attachments per configuration have OFF/ON maximum error zero (fourteen comparisons total). These controlled scene pixels do not compare every product-view pixel or resized history content under delayed GPU stress.

Final evidence: `Build/Verification/Phase43/RG7ProductViews-FinalDock-20261008/{rg7-product-result.json,comparison-Debug.json,comparison-Release.json,Debug-On,Debug-Off,Release-On,Release-Off}` and `rg7-product-views-final-dock.log`. Final builds: `rg7-{debug,release}-product-dock-migration-build.log`. Earlier failed/intermediate matrices remain preserved. RG7 remains in progress, earned effort zero and aliasing default OFF; peak VRAM and preparation/performance acceptance were not measured by this slice.

## Transient preparation CPU instrumentation slice

Graph stats now retain `transientPrepareCpuMs`, measured with `steady_clock` across `CreateTransients`, including lifetime discovery, allocation/cache lookup and native resource creation. The scope excludes barrier planning, command recording and GPU execution; wall time includes any driver/scheduling waits. RAII records early failure returns too, and existing compile/reset stats lifecycle clears old measurements. This adds two clock reads per preparation. Capture output exposes `measurement.cpuTransientPrepareMs` with an explicit scope, plus `graph.transientPrepareCpuMs`; this does not replace the existing compile/record measurements.

VS 2026/v18 Debug/Release builds and existing Editor full RenderGraph regressions passed. Both configurations passed all eleven GPU modes, six failure cases, two lifetime cases and native WARP removal, with validation zero and exit zero. Each GPU mode reports one finite, positive preparation observation; `audit-rg7-preparation.ps1` validates the complete mode set and terminal command results. The regression wrapper accepts a fresh output directory to preserve previous artifacts. Evidence: `Build/Verification/Phase43/RG7PreparationCPU/{Debug,Release}/{preparation-result.json,results.jsonl,binary-metadata.json}` and `rg7-{debug,release}-prepare-cpu-{build,acceptance}.log`.

These eleven observations per configuration prove instrumentation works in the native Editor fixture, including OFF, ON, warm reuse and eviction/recreation. They are not repeated equivalent product workload samples and do not establish preparation cost acceptance or a speedup. The new capture fields compiled, but a fresh product capture matrix with those fields has not been run in this slice. Release linking also reported LNK4020 for Utility_Framework PDB type records; runtime tests passed, while those Release debugger type records remain a build-symbol limitation.

Next preparation/performance acceptance must collect repeated same-input, same-resolution OFF/ON product samples with cold and warm cache states separated, retained executable/runtime hashes, matching pixels and bounded validation policy. Peak memory must retain separate graph requested bytes, unique live heap bytes, free cache bytes and periodically sampled process device-budget usage over the same workload interval. The latter is a sampled usage maximum, not proof of an exact resident VRAM peak. Neither single snapshots nor adding independent graph allocation totals establishes whole-process peak savings. RG7 remains in progress, earned effort zero and default OFF.

## Release product repeated preparation observations

The existing product Editor harness now optionally collects controlled post-readiness preparation captures before view/scene/resize checks. The product wrapper can select configurations while retaining Debug/Release as its default. `audit-rg7-product-preparation.ps1` checks completed runs, same executable/runtime, GPU validation, warm ON heap reuse and finite preparation/compile/record measurements. All sample inputs must match, including camera/lights/environment/IBL, material values, mesh generation, world matrices and skin pose bytes. The shared pixel comparator now checks world/pose/model generation as well as material inputs.

The current VS 2026 Release binary passed two fresh product Editor processes (ON then OFF), eight measured captures each at 1498×730, after material preparation completed. All sixteen input records match and all eight OFF/ON pairs have seven attachments with maximum error zero (56 attachment comparisons). The initial readiness capture also passes OFF/ON comparison. Every warm ON sample reuses six heaps / fifteen resources, creates zero heaps and issues zero allocation queries. Both runs pass independent three-view graph readiness/lifetimes, scene replacement and resize with GPU validation problems/drops zero and process exit zero. These measurements contain diagnostic readbacks and run with GPU validation enabled.

| Release mode | Samples | Prepare CPU median / p95 (ms) | Compile CPU median / p95 (ms) | Record CPU median / p95 (ms) | Sampled device usage max (MiB) |
|---|---:|---:|---:|---:|---:|
| OFF | 8 | 0.0053 / 0.0089 | 0.1516 / 0.2720 | 2.72155 / 3.7702 | 1116 |
| ON | 8 | 0.0244 / 0.0324 | 0.1802 / 0.3303 | 2.68475 / 4.8135 | 1109 |

Warm ON preparation has an observed median increase of 0.0191 ms. The small record-time difference does not prove a frame speedup; preparation/compile/record scopes must not be added as disjoint frame costs. With eight samples, the reported nearest-rank p95 is the sample maximum. One process per mode and fixed run order do not characterize process variation, cold creation or production performance without validation/readbacks. The eight device-budget snapshots per mode observe a 7 MiB usage difference; MiB rounding, cache retention and other process resources prevent attributing it to exact transient resident peak savings. This is not continuous peak sampling or live unique heap/cache byte accounting.

Evidence: `Build/Verification/Phase43/RG7ProductPreparation-20261008/{product-preparation-result.json,rg7-product-result.json,preparation-comparison-Release-0.json,...,preparation-comparison-Release-7.json,Release-On,Release-Off}` and `rg7-product-preparation-acceptance.log`. Executable/runtime identity matches the preceding preparation instrumentation builds. `performanceAccepted=false`, `exactPeakVramMeasured=false`, RG7 progress/earned zero and default OFF remain explicit. Next: unique live heap/free-cache byte accounting and finer device-usage sampling across cold/warm, scene/resize and delayed retirement, then reversed-order independent process repeats and a product performance gate. Debug repeated product preparation observations have not been run in this slice.

## Unique shared-heap ownership accounting slice

`RGTransientPool::GetAliasHeapMemory` and the graph accessor now distinguish retained, free-cache and leased shared native heap bytes/counts. A shared accounting state is registered once after each successful native heap creation, before placed-resource creation; group destruction unregisters it after releasing resources and its heap. Cached borrowing/return changes classification without creating another heap count. Retained equals cached plus leased. Leased means ownership outside the free cache, including partial creation, GPU-delayed/quarantined graphs and an externally retained group; it does not imply submitted/completed GPU work. Snapshot totals are protected by a mutex, while pool operations/queries retain the producer or synchronized-lifecycle contract.

The peak is the cumulative high-water mark of retained group-owned shared native heap allocation bytes for that accounting state. Reset/cache clear do not erase its history. It excludes imported/history and committed resources, independent native heap owners, hardware residency and driver-deferred memory. A pool's accounting is visible through multiple graph accessors: do not add per-view snapshots of the same pool. A stable accounting-domain identity/deduplication is still needed before aggregating multiple product pool/view snapshots into an engine-wide sample.

VS 2026/v18 Debug/Release builds and existing Editor full RenderGraph regressions passed, including eleven GPU modes, six failure cases, two delayed-lifetime cases and native WARP removal, with GPU validation zero and exit zero. Warm reuse preserves two unique heaps / 128 KiB. Eviction/drain reaches zero. Cache clear with an extra group owner reports cached zero but still retains its 64 KiB heap until the last group owner releases it. Partial creation failures check transient high-water marks and zero retained bytes after cleanup (query failure zero; second-heap/second-placement failure 64 KiB; fourth placement/stale activation 128 KiB).

The actual GPU fence gate verifies three independent graphs retain three heaps / 192 KiB with cached zero until GPU completion, then transfer all 192 KiB to the cache without increasing retained bytes. The post-native failure case retains its 64 KiB lease even after its completion value is observed, until explicit verified GPU idle. Native WARP removal similarly retains its 64 KiB quarantine until abandonment. Final drains leave retained/cache bytes and heap counts zero while cumulative peaks remain intact. These byte checks are combined with native resource/heap weak-owner release and payload/validation checks.

Evidence: `Build/Verification/Phase43/RG7HeapMemory/{Debug,Release}/{heap-memory-result.json,results.jsonl,binary-metadata.json}` and `rg7-{debug,release}-heap-memory-{build,acceptance}.log`; `audit-rg7-heap-memory.ps1` checks the new cache and GPU-retention acceptance markers. Capture serialization exposes `measurement.aliasHeapMemory` with the explicit `unique-group-owned-native-heaps-not-resident` scope. This new capture payload has compiled but has not been checked by a fresh product capture matrix in this slice. The preceding Release product timing/usage observations remain historical for their runtime hash; counter updates have not been performance-remeasured.

Next: accounting-domain identity for view/pool deduplication, byte-precision process device usage and synchronized repeated product samples across cold/warm, scene/resize and delayed retirement. Continuous usage sampling, committed-resource ownership accounting and exact resident peak savings remain unaccepted. RG7 stays in progress with earned effort zero/default OFF.

## Accounting domains and byte-precision product observations

Each initialized alias-heap accounting state now has a process-local nonzero `domainId`. The identity survives reuse, reset and cache drain; graphs sharing a pool publish the same identity. An uninitialized accounting state reports zero. Captures and frozen graph diagnostics expose the identity, retained/cache/leased bytes and cumulative ownership peak. IDs are not persistent across processes. A product auditor keeps one observation per nonzero domain; view records publish at different frames and are not added into a simultaneous engine peak.

`RHIVideoMemoryInfo` now retains raw used/budget bytes and separate usage/budget availability alongside the compatible MiB fields. DX12 reports the raw successful budget query. Vulkan marks usage unavailable without its memory-budget extension and retains capacity fallback for budget; Vulkan runtime acceptance has not been performed here. The DX12 adapter exposes the full backend-neutral value without expanding resource-creation services. Device usage remains a sampled device-budget observation, not shared-heap ownership or exact residency.

VS 2026/v18 Debug/Release builds and existing Editor full RenderGraph regressions passed again: eleven GPU modes, six failure cases, two lifetime cases and native WARP removal, validation zero and exit zero. Added native assertions verify stable pool identity across warm reuse/cache drain and three GPU-delayed graphs, including the frozen diagnostic snapshot. Evidence: `Build/Verification/Phase43/RG7MemoryDomains/{Debug,Release}/{heap-memory-result.json,results.jsonl,binary-metadata.json}` and `rg7-{debug,release}-memory-domain-{build,acceptance}.log`. Release LNK4020 Utility_Framework PDB type warnings remain; runtime acceptance does not repair debugger type records.

The fresh Release product ON-then-OFF processes each completed eight post-readiness controlled Scene captures at 1498×730. All sixteen inputs match; eight pairs × seven attachments have maximum error zero. Initial readiness comparison, three-view readiness/lifetimes, scene replacement and resize also pass; validation problems/drops zero and exit zero. Every ON sample reuses six heaps/fifteen resources with zero heap creation/allocation queries. Capture timing includes diagnostic readbacks and GPU validation.

| Release mode | Unique shared domains across three views | Retained shared heap bytes in eight captures | Free-cache bytes | Sampled device usage maximum (bytes) | Prepare CPU median / p95 (ms) |
|---|---:|---:|---:|---:|---:|
| OFF | 0 | 0 | 0 | 1,171,038,208 | 0.00565 / 0.0068 |
| ON | 1 | 16,777,216 | 0–8,388,608 | 1,163,436,032 | 0.02585 / 0.0331 |

Scene/Game/Preview all report ON domain 1 and 16 MiB retained at their individual observation frames. Their cache/lease classifications vary; summing them would triple-count the same accounting state. The reported cumulative shared ownership peak is 77,725,696 bytes, accumulated since state creation; this excludes committed/imported allocations and is not a resident or whole-process peak. The two sampled device maxima differ by 7,602,176 bytes, which cannot be attributed solely to transient residency. MiB rounding no longer hides sub-MiB changes. Preparation median increases by 0.0202 ms in this run; record median OFF/ON is 2.9082/3.4069 ms and p95 6.3008/6.2735 ms. These single-process, fixed-order observations do not establish a speedup or a product performance gate. Earlier preparation observations above belong to their earlier runtime and remain historical.

Evidence: `Build/Verification/Phase43/RG7ProductMemoryDomains-20261008/{product-preparation-result.json,rg7-product-result.json,preparation-comparison-Release-0.json,...,preparation-comparison-Release-7.json,Release-On,Release-Off}` and `rg7-product-memory-domain-acceptance.log`. Each case retains `memory-samples.json`, capture manifests, per-view frozen graphs and executable/runtime hashes. Release runtime SHA-256 is `298A27D30F46898811323733AC0E52F9F90FDEA527B58F2D006E27F5A8E60A79`, matching the native regression binary. `memoryAccountingAudited=true`, `performanceAccepted=false`, `exactPeakVramMeasured=false`. Debug repeated product memory captures have not been run in this slice.

Next: synchronized continuous device-usage sampling across cold creation, scene/resize and delayed retirement, committed-resource ownership accounting, reversed-order independent process repeats and representative performance acceptance. These sixteen capture-boundary samples do not close lifecycle peak sampling. RG7 remains in progress, earned effort zero/default OFF.

## Fixed RG7 closure checklist (2026-10-08)

This checklist supersedes the historical "Next" paragraphs above. The baseline is RG7's existing allocation/lifetime/compatibility/initialization contract and the roadmap's memory/cost acceptance table. Extensions discovered while validating are not automatically new closure gates. Existing evidence stays accepted within its tested scope; regression reruns after relevant changes do not count as new progress. The final closure resolves C4-C6 below; RG7 is done/default OFF with 14 planned effort days recovered.

| Gate | Status | Evidence / remaining action |
|---|---|---|
| C1: buffer/texture lifetime, serialized non-overlap sharing, alignment/usage compatibility, imported/history exclusion and committed fallback | Accepted within DX12 implemented scope | Existing D/R GPU fixtures and product graph diagnostics. Unsupported resource usages retain fallback; broad support is not a prerequisite. |
| C2: alias activation/poison and failure-safe ownership | Accepted within supported scope | 11 GPU modes, six failure cases, post-native failure retention, WARP loss and final drain. New failure of this contract reopens this gate; additional failure categories alone do not. |
| C3: multi-frame/view correctness and pixel parity | Accepted within bounded scope | Three-frame GPU gate/history/late reads plus D/R product Scene/Game/Preview, scene replacement/resize and controlled output parity. No unlimited stress duration is implied. |
| C4: memory ownership and lifecycle usage measurement | Complete within available diagnostics | 17,684 continuous records, all queries valid, maximum gap 130ms; distinct shared heap ownership and DXGI usage. Per-resource committed/residency attribution is explicitly unavailable; see final closure. |
| C5: equivalent-workload cost and adoption verdict | Complete; keep default OFF | Four independent Release processes in both orders, 32 captures and 126 repeated/cross-order attachment comparisons with error zero. No demonstrated CPU/lifecycle usage benefit. |
| C6: product control and closure record | Complete | Existing opt-in/committed fallback, default OFF, limitations and canonical status/effort recorded in RenderRg7Closure20261008.md. |

Execution order is C4 -> C5 -> C6. Any new blocker must identify a violated C1-C6 contract and current evidence; optional coverage must remain separately tracked. Existing whole-Editor regressions run when relevant native code changes; harness/document-only edits do not require rebuilding or repeating the full native matrix.

### Deferred coverage (does not block this bounded DX12 RG7 closure)

- RG7-H: actual native OOM, spontaneous hardware loss/backend rebuild and genuine native Signal HRESULT failure with uncertain execution. Preserve these as failure/recovery hardening work; deliberate removal and injection do not establish these results.
- RG7-S: longer product GPU-delayed multi-view/history content stress, broader scenes/resolutions and poison support for additional resource forms. Current supported usages and bounded evidence remain explicitly limited.
- Vulkan aliasing/product runtime acceptance remains a separate backend gate; the byte-precision Vulkan query change has no runtime acceptance here.
- RG8: cross-queue shared-heap handoff execution/fence dependencies. Current sharing assumes serialized GPU execution; introducing overlap requires RG8 acceptance.

Historical checklist reconciliation initially claimed no native implementation or fresh runtime measurement. Final measurement/closure is now recorded in RenderRg7Closure20261008.md. The previously collected capture-boundary samples do not satisfy C4's uninterrupted series. The current historical performance observations do not satisfy C5's independent reversed-order repeats. The subsequent final closure recovers those 14 planned effort days; scope reconciliation alone did not earn them.
