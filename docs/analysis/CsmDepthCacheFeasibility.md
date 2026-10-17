# Bounded shadow-depth cache: implementation feasibility checkpoint

Inspected base: `d6fa3fae948f2bc17ae66fd87e667cc799142b18` (PR #115).
Branch: `perf/bounded-shadow-depth-cache`.

This is a source audit and a proposed implementation split. It does **not**
implement shadow-content reuse, measure a speedup, or complete the cache phase.
The renderer remains unchanged and caching remains unavailable.

## Decision

Do not implement the previous proposal as a local early return in
`EnhancedShadowPass::Declare`. A safe complete-map cache needs a coordinated
transaction across both shadow writers, the live view binding, and the native
submission result. Existing APIs provide useful building blocks; this does not
require replacing the render graph or adding a new graphics backend. It is,
however, a larger renderer integration than a self-contained shadow-pass cache.

Start with cache-disabled eligibility/key instrumentation in that coordinated
owner, then introduce opt-in reuse only after the key has measurable eligible
static hits. Unknown inputs force redraw. Do not enable a renderer optimization
whose benefit has not been measured.

## Findings in the current implementation

| Contract | Evidence | Consequence |
| --- | --- | --- |
| Two producers write one map | `EnhancedSceneRenderer.cpp:2362-2364` calls `shadow.Declare`, then `graphMaterials.DeclareShadow`. `MaterialGraphSceneHost.cpp:1097` adds `LX.Scene.Shadow`, loading the existing depth slices. | A cache decision must cover both producers. Skipping just legacy recording leaves graph work active and can preserve stale removed graph casters. Skipping just graph recording loses its depth when legacy clears. |
| Shadow owner is shared across views | `EnhancedSceneRenderer.cpp:364` defines one `LivePipeline::shadow` beside the view array; Vulkan has the same arrangement at line 508. `EnhancedFrameContext` at `EnhancedRenderPass.h:419` has frame/scene IDs but no stable view ID. | Pass identity, camera pointer, or array index is not a view identity. Supply the existing live `view.key` and backend generation explicitly. A one-entry policy must pin one eligible view or miss when another view is active. |
| Import writeback describes planned state | `EnhancedRenderGraph.cpp:951` calls `PlanBarriers` during `Compile`; line 864 writes the caller's state pointer there, before execution/submission. | Keep planned and committed resource states separate. Compile or record failure must not publish the planned state as physical GPU state. The existing API can write into transaction-local storage; changing global graph semantics is unnecessary. |
| Enqueue is not successful submission | DX12 at `EnhancedSceneRenderer.cpp:4576-4587` and Vulkan at lines 1013-1021 enqueue a recorded batch and pass its ticket to `PublishSubmittedCache`. `MaterialGraphSceneHost.cpp:1811-1929` defers publication until the exact ticket succeeds. | Reuse this pattern, including failed tickets. `EndFrame` returning and a nonzero completion value alone are insufficient evidence that cached depth exists. CPU ticket completion and GPU fence retirement are separate gates. |
| Resource release does not wait | `IRenderDeviceServices.h:244` exposes `ReleaseTexture`, explicitly making the owner responsible for fence retirement. | Track the last **reader** as well as writer. A cache hit still extends lifetime. Release only after the corresponding completion point, including view removal and shutdown. |
| Typed model identity exists, but the pass only receives a borrowed view | `RHIModelMeshView` at `IRenderDeviceServices.h:335` has the complete model/mesh/generation tuple plus raw data pointers. `EnhancedDrawItem` also permits legacy mesh inputs and an arbitrary `geometryKey`. | Use the typed tuple and retain the producer's immutable generation owner in the cache transaction. Do not use the hashed batch key as exact content identity. Legacy/unowned inputs need an explicit eligibility exclusion or a validated content snapshot. |
| Graph input revision is intentionally fresh | `MaterialGraphSceneInput.cpp:105-115` increments a revision on every seal, even for an unchanged view. | Using the revision or sealed-input pointer as a cross-frame key gives no warm hits. Removing it without replacing its content dependencies is unsafe. Compare stable contents while retaining the immutable owners. |
| Useful graph cache precedents already exist | `MaterialGraphMeshSurface.cpp:793-812` compares static identity, pipeline, world and bones, checks completion/state, and retains the old owner. `MaterialGraphRenderBindings.cpp:139` rejects textures without CPU generation ownership. | Reuse these contracts instead of claiming graph assets have no generations. Audit texture identity at the selected shadow bindings, including sampler and material values. A descriptor version is recording lifetime, not texture content. |

Line numbers refer to the inspected base, not a future implementation.

## Concrete implementation boundary

Introduce one device/backend-scoped cache coordinator, called from the common
live shadow node, with the pass supplying cascade data and the SceneHost supplying
the **selected prepared** shadow dependencies. Requested graph material instances
can differ from the accepted fallback, so requested inputs alone are not a key.
Keep the entry single-view and opt-in; another view uses the ordinary transient
map without evicting an in-use retained map.

The coordinator owns one three-layer 2048-square D32 texture (48 MiB of logical
depth texels, excluding allocation alignment, views and dependency metadata).
The bound refers to retained cache storage, not total transient shadow memory.
Retiring entries still count against the bound. On replacement while retirement
is pending, redraw into an ordinary transient resource; do not allocate another
retained 48 MiB map or insert a GPU wait to hide the budget problem.

The first eligible subset should require verifiable immutable typed geometry and
owned material resources. Reject unversioned legacy sources and unsupported
material dependencies. Skinned input can either compare the exact owned palette
or initially force miss; it must never hit based only on animator identity.
Do not omit graph draws silently: either key the selected graph shadow stream or
make their presence an explicit unsupported-input miss.

The key needs exact camera/projection, scene/view/backend identity, selected light
identity and direction, cascade matrices/coverage/settings, ordered caster
membership, full geometry tuple/layout/LOD, bounds, world/pose, and the selected
shadow material/program/coverage/alpha texture/sampler identities and values.
Hashes may accelerate equality but cannot replace collision-safe comparison.
NaNs, missing owners and unknown producer versions force miss. Compare meaningful
fields or initialized payloads, not unspecified struct padding.

The transaction retains key owners, texture ownership, planned final state and
the exact batch ticket. Only successful submission can publish content/state.
Initially require the producer GPU completion point before reuse, matching the
existing conservative world-geometry cache. A later same-queue optimization can
relax that requirement only with explicit queue-order evidence. No partial-map
publication, no publication after abort, and no in-place overwrite while a hit
or producer is unresolved.

## Required executable acceptance, not yet run

1. Cache-disabled instrumentation: eligible frames, would-hit, miss reason,
   key-build CPU time, retained bytes, and both writers' GPU time. Measure a
   static eligible scene and a representative moving/graph/skinned scene.
2. Submission state tests: compile failure, recording exception, rejected batch,
   failed ticket, incomplete writer, incomplete reader, view destruction and
   backend recreation. Planned state must never escape an aborted transaction.
3. Exact depth and final-color cache-on/off comparisons for camera micro-motion,
   orthographic projection, light selection/direction, cascade settings, caster
   add/remove/transform/bounds, pose, geometry generation/LOD, alpha contents,
   sampler, coverage and selected material fallback changes. Unknown inputs
   must count a miss and render normally.
4. DX12 and Vulkan with three genuinely pending frames, two alternating views,
   cache eviction/retirement and the retained-memory cap. Verify both producers
   are skipped on a hit and both execute on a miss as appropriate.
5. Warm static and dynamic performance runs without overlapping owned workloads:
   CPU key cost, GPU shadow cost, hit ratio and retained bytes. Leave opt-in
   disabled by default unless saved work exceeds bookkeeping cost. Do not use
   PR #115's overlapping visual-capture timestamps as performance evidence.

## Validation performed for this checkpoint

Fetched upstream master and created an isolated worktree at the inspected merge.
Read the actual producer, import/barrier, submission/publication, resource-release
and dependency-identity implementations above. No renderer code or original
checkout files were changed. No new build, GPU invalidation matrix, cache-on/off
equivalence run or isolated timing run was performed because there is no cache
implementation in this checkpoint. PR #115 tests validate its renderer fixes;
they do not validate this proposed cache.
