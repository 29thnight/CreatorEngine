# Rendering pipeline allocation audit — 2026-10-22

## Scope and baseline

This audit follows the report of increased local execution time after the rendering pipeline changes in PR #190. It measures repeated allocations in the merged implementation, removes avoidable temporary ownership/storage, and preserves the existing CPU and GPU lifetime contracts.

The baseline is local commit `7b5e03da9802c903eaeacc48278d23585b36cfb6`. Its tree is `cc90afe59fffcc09e05d1fc18ccf51944b167a18`, identical to the merged master commit `24db52e894c820cb01eeee71297c5654b579c5a9`. The comparison is therefore against the merged implementation, not against the architecture before PR #190.

The fixtures compile production graph, feature, light-selection, recorder, translator and submission code. Native device services, encoders, queues and fence observation are replaced at documented CPU test boundaries. These measurements establish allocation counts in those scopes. They do not establish Editor frame time, GPU execution time, visual correctness or the cause of the reported full-application slowdown.

## Allocation results

Counts below are requests per warmed frame or submission. `new` includes C++ scalar, array and aligned allocation entry points; `mi` is counted separately. Cold construction and capacity growth are excluded by explicit warmup. The graph comparison uses 24 resources, 32-pass queue-planning cases, and separate queue-submission fixtures. Each fixture describes its exact boundary in the linked JSON.

| Production scope | Before: `new` / `mi` | After: `new` / `mi` |
|---|---:|---:|
| Reused graph declaration | 0 / 0 | 0 / 0 |
| Plain graph compile | 1 / 1 | 0 / 0 |
| Subresource graph compile | 455 / 0 | 0 / 0 |
| Aliased transient graph compile | 681 / 1 | 0 / 0 |
| Threshold queue planning | 6 / 0 | 0 / 0 |
| Overlap queue planning | 7 / 0 | 0 / 0 |
| Graphics queue submission, mock-native allocation excluded | 21 / 0 | 1 / 0 |
| Graphics/compute queue submission, mock-native allocation excluded | 90 / 1 | 1 / 0 |
| Feature declaration without observer | 0 / 0 | 0 / 0 |
| Feature declaration with six-reference observer | 1 / 0 | 0 / 0 |
| Selection and packing of 256 lights | 3 / 0 | 0 / 0 |
| Async software submission, one target / 1,554 commands | 10 / 0 | 1 / 0 |
| Async software submission, eight targets / 12,432 commands | 15 / 0 | 5 / 0 |

The feature comparison measures 128 frames after warmup. The observer originally requested 48 bytes per frame. Light selection originally requested 44,032 bytes per frame. The async submission comparison measures 33 samples after 16 warmups, and all allocation samples agree within each case.

The remaining graph-submission allocation is the 56-byte completion lease that prevents graph mutation while native batches retain it. Async software submission retains one aggregate until GPU retirement. The eight-target case additionally retains four allocations in the existing job scheduler. Moving these objects into an arena reset at CPU submission would violate their lifetime contracts.

The existing software-recording measurement remains `new=0`, `mi=9` for the eight-target fixture. Recording is outside the async submission measurement above. This change does not claim a zero-allocation renderer or zero allocation across every recording and asset-preparation path.

## Changes and lifetime decisions

### Graph compilation and submission

Subresource validation now fills graph-owned cell and boundary scratch buffers. It reuses validated cell geometry for range-capable backends and appends barriers directly into retained pass storage. Resource metadata validation and conservative lowering remain active.

Transient alias proof builds the resource-use index and dependency adjacency once per compile in a reusable graph arena. Every candidate still requires all-use happens-before proof. Candidate sorting uses resource order as an explicit tie break, preserving the previous stable ordering without a temporary sorting allocation.

Queue plans, simulation candidate order, bitsets and diagnostic pass-list capacity survive across frames. Submission-local positions, frontiers, recording records and wave arrays use a separate graph arena. Barrier specialization uses a different arena, because it runs while submission scratch remains live. Recording and CPU admission join before these local objects are destroyed. Native batches and their completion lease retain their own owners through GPU retirement.

Declaration buffers are valid until the guarded graph `Reset`. Starting another declaration-packet batch no longer rewinds storage that an earlier declaration still borrows.

### Feature callbacks and light selection

`LivePipelineDesc::DeclareAll` accepts a borrowed synchronous functor without constructing an owning `std::function`. The compatible `std::function` overload remains available. Both live render backends use the no-observer path when capture is inactive. Vulkan's synchronous preparation callback is also passed directly as a functor.

Light selection retains both output and score/index scratch. It sorts small score/index entries and packs each selected shader light once. Original input ordinal breaks equal-score ties; NaNs sort last within their directional/local group so the comparator remains a strict ordering. `RenderScene::CopyLightProxySnapshot` fills caller-owned capacity while preserving the existing map-lock/owner-copy contract.

### Scene input and material declarations

The live renderer retains the sealed immutable gizmo owner instead of copying its icon and line arrays twice. Shadow draws, lights, sprites and UI rectangles transfer into a preparation slot by swapping buffers. A slot is writable only after all outside readers release it. Previously owned capacity returns to the producer; the temporal motion diagnostic reads the prepared sprite array after transfer. Decals and geometry pins remain copied into prepared storage because their producer data is shared by multiple views.

Shadow resource lists use blackboard spans. Blended command handles live in graph declaration storage, and `SceneLayerWork` records the graph and declaration epoch. Consumers reject stale or unbound nonempty spans before reading them. Public blended-draw indices are copied once into graph declaration storage before callbacks retain spans; callers may still supply temporary vectors. Duplicate-index checks retain their rejection behavior with arena scratch instead of per-index tree nodes. The stream's empty check no longer constructs a `ForwardDraw` vector.

Material preview uses a bounded three-element array. The shadow-index mapping used only by controlled replay is retained and populated only when replay is active.

### Async RHI submission

Software commands, translated output, pool/translator owners and GPU resource pins share one retirement aggregate. Synchronous callback wrappers borrow it while the queued entry or retirement record owns it. The software batch is cleared even when a custom translator throws, so CPU arena admission credits follow actual storage release.

Preparation metadata is embedded in the ticket state. A bounded 64-slot cache reuses a ticket only when the cache is its sole owner, after an acquire fence observing the last owner's release. Queued, preparing, running and externally held tickets prevent reuse. Native recorded-batch tickets bypass this cache so native payloads are not retained past owner teardown or destroyed under the submission mutex.

The three-entry admission queue retains vector capacity. Native command target order uses an eight-element inline array, matching the already-enforced target limit. Synchronous recording/translation callbacks use borrowed wrappers only at APIs that join before returning.

## Validation and limits

Production CPU graph tests cover subresource ranges, conservative lowering, alias ordering, queue failure and partial-submission handling, declaration arena lifetime, changing owned-target workloads, and rejected-plan reset. The allocation runner enforces zero warm requests for the measured declaration/compile/planning scopes and one required completion lease for graph submission.

Feature tests cover borrowed/mutable/compatible observers, imported-resource metadata rejection, contributor isolation, light ordering/culling, equal-score stability, NaN ordering and warmed allocation budgets. Async RHI tests cover serial and parallel translation, 96 retained completed tickets, failure preservation across reuse/shutdown, finite GPU retirement with tickets still held, and a throwing translator that has not consumed its software batch. AddressSanitizer and UndefinedBehaviorSanitizer runs are included. LeakSanitizer is disabled because this execution environment restricts process inspection.

The actual `PreparedViewInputPool` test also passes with ASan/UBSan. It verifies that in-flight snapshots and other views cannot be overwritten, that exhaustion defers preparation, and that released slots retain capacity.

The Windows Editor/DX12 application and native material pixel fixtures were not built or executed in this Linux environment. The material blend fixture now covers stale span tokens and a temporary public index vector, but those added native cases remain unrun here. Scene integration was reviewed against every consumer of the transferred arrays and both backend preparation paths. Native frame-time and visual acceptance still require the same scene, capture settings, build configuration and GPU on the user's machine.

Retained buffers trade repeated allocation for their warmed high-water capacity. Larger workloads may allocate when they exceed that capacity. The submission ticket cache is bounded; tickets held beyond its capacity allocate independently and remain immutable.

## CPU timing observations

The final graph comparison builds both frozen variants before measurement, alternates their execution order, and performs nine runs of 1,024 measured iterations per variant after eight warmups. Other team compiler processes had finished. The table reports the median of the nine per-run means, in microseconds per iteration; the raw JSON retains every run and its input/executable hashes.

| CPU graph scope | Before, µs | After, µs |
|---|---:|---:|
| Plain compile | 1.64 | 1.49 |
| Subresource compile | 19.78 | 7.95 |
| Alias compile | 22.31 | 7.33 |
| Threshold queue planning | 3.00 | 2.97 |
| Overlap queue planning | 112.29 | 101.16 |
| Graphics queue submission | 2.57 | 2.39 |
| Graphics/compute queue submission | 12.82 | 10.24 |

These CPU fixtures show lower compilation time, especially in the range and alias cases. Host scheduling still produces outliers, and the smaller differences should not be treated as a native performance guarantee.

The separate async submission fixture includes coordinator scheduling and a one-millisecond mock-retirement polling interval. Its elapsed times do **not** demonstrate a general speed improvement despite the reduced allocation count:

| Async CPU fixture | Before p50 / p95, µs | After p50 / p95, µs |
|---|---:|---:|
| One target | 1,167.55 / 1,252.66 | 1,151.08 / 1,430.23 |
| Eight targets | 1,491.99 / 1,712.54 | 1,493.36 / 1,712.24 |

## Reproduction

Run from the repository root with a GCC/Clang C++23 compiler. The published merge commit below has the same tree as the measured local baseline. Pass local dependency checkouts; the runners do not download them. Tested dependencies were enkiTS v1.12 (`0289cf6ffce86697e1f46cb4eeca1c946bffec71`) and Vulkan-Headers v1.4.328.

```sh
ALLOCATION_BASELINE=24db52e894c820cb01eeee71297c5654b579c5a9
ALLOCATION_ENKITS=/path/to/enkiTS
ALLOCATION_VULKAN_HEADERS=/path/to/Vulkan-Headers

python3 Tools/regression/verify-render-graph-allocations.py \
  --baseline-ref "$ALLOCATION_BASELINE" --repetitions 9 --iterations 1024 \
  > /tmp/graph-allocation-comparison.json

python3 Tools/regression/verify-live-pipeline-contracts.py \
  --compare-feature-core-ref "$ALLOCATION_BASELINE" > /tmp/feature-allocation-comparison.json

python3 Tools/regression/verify-rhi-software-portable.py \
  --enkits "$ALLOCATION_ENKITS" --baseline-ref "$ALLOCATION_BASELINE" \
  --skip-commands-probe --output /tmp/rhi-allocation-comparison.json \
  --work-dir /tmp/rhi-allocation-comparison

ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-render-graph-program.py --sanitize
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-live-pipeline-contracts.py --sanitize
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-rhi-software-portable.py \
  --enkits "$ALLOCATION_ENKITS" --sanitize --output /tmp/rhi-allocation-sanitized.json \
  --work-dir /tmp/rhi-allocation-sanitized
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-rhi-software-queue.py \
  --enkits "$ALLOCATION_ENKITS" --sanitize --output /tmp/queue-allocation-sanitized.json
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-vulkan-software-commands.py \
  --enkits "$ALLOCATION_ENKITS" --vulkan-headers "$ALLOCATION_VULKAN_HEADERS" --sanitize \
  --output /tmp/vulkan-allocation-sanitized.json --keep-build /tmp/vulkan-allocation-sanitized
```

Use fresh paths for runner-owned build directories. The Vulkan header argument is the checkout root, not its `include` directory.

## Evidence

| Receipt | Result and scope |
|---|---|
| [Graph comparison](rendering-allocation-20261022/graph-comparison.json) | Ten allocation budgets pass; paired timing and immutable source/executable hashes |
| [Graph correctness](rendering-allocation-20261022/graph-correctness.json), [sanitizers](rendering-allocation-20261022/graph-sanitized.json) | Production graph program, range, ownership, queue and alias checks pass |
| [Feature comparison](rendering-allocation-20261022/feature-comparison.json), [sanitizers](rendering-allocation-20261022/feature-sanitized.json) | Baseline 1,008 checks; current 1,027 checks; all current measured warm scopes allocate zero |
| [RHI comparison](rendering-allocation-20261022/rhi-comparison.json) | Same-probe baseline/current: 34,399 checks, 1,759 scenarios each |
| [Final RHI source](rendering-allocation-20261022/rhi-current.json), [sanitizers](rendering-allocation-20261022/rhi-sanitized.json) | Exactly formatted final source passes 34,399 checks; sanitizer run includes the existing command probe |
| [Owned queue sanitizers](rendering-allocation-20261022/queue-sanitized.json) | 5,474 checks pass |
| [Vulkan CPU boundary sanitizers](rendering-allocation-20261022/vulkan-sanitized.json) | 524 checks pass; warmed binding allocation is zero; no native GPU execution |
| [Header formatting equivalence](rendering-allocation-20261022/header-format-equivalence.json) | Paired RHI/owned-queue receipts precede a header-only whitespace change; exact non-whitespace bytes and preprocessor directives match |
| [RHI summary](rendering-allocation-20261022/rhi-summary.json) | Allocation scopes, remaining owners, timing limits and source hashes |
| [Core/material source hashes](rendering-allocation-20261022/feature-material-source-hashes.json) | Final reviewed sources; native material fixture remains unrun |
| [Final verification summary](rendering-allocation-20261022/verification-summary.json) | Final changed-source manifest, matching tested source identities, prepared-view ASan/UBSan result and static checks |

The Core comparison isolates old/current Core code against one common frozen Graph/RHI/fixture snapshot. Other contributors' later edits are explicitly listed in the receipts and cannot affect those frozen builds. The final graph pair reports no source changes after snapshot. RHI normal and sanitizer receipts verify its final formatted production source directly.
