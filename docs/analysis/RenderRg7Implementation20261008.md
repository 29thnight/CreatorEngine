# RG7 transient buffers and placed heap sharing — 2026-10-08

RG7 is in progress. Q0, RG8 and RG9 remain pending and follow the agreed order.

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

## Remaining RG7 acceptance

- Native allocation/submission/activation failure injection and explicit poison. Invalid placement contracts and deliberate overlap/extended-lifetime guards are already checked.
- Stress retained in-flight graphs across multiple frames and explicit history/late-reader transitions. Parallel fixture recording, imported buffer retention and exclusion in the static Release product capture are already checked.
- DX12 Debug live product parity, Scene/Game/preview at representative resolutions, pooled/placed allocation CPU cost and peak committed/resident VRAM. The small static Release Editor slice above is accepted separately.
- Representative workload performance and cache behavior beyond the bounded reusable-group fixture. No performance benefit is assumed from fewer allocated bytes or fewer native calls alone.
- Product setting surface and default adoption decision after acceptance.
- RG8 must add execution/fence dependencies for shared heap handoffs before introducing cross-queue overlap. Ordinal non-overlapping lifetimes assume the current serialized GPU execution order.
