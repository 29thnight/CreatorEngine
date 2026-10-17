# METEOR camera compute meshlet work — 2026-10-13

## Implementation

- The camera prepares one metadata range per geometry/material bin. CPU preparation no longer constructs a candidate for every meshlet.
- Compute tests the actual transformed/posed vertices against the camera clip planes and atomically compacts surviving meshlet ordinals.
- `METEOR.CameraMeshlet.Reset/Cull` produces the count-prefixed frustum stream for the depth prepass. The B1 follow-up adds `METEOR.CameraMeshlet.HZB.Reset/Cull` for the final surface stream. Both use GPU-finalized indirect arguments; mesh shaders do not replay culling.
- Camera instance HZB rejection remains in the GPU owner gate; the final meshlet list additionally rejects occluded posed bounds. Shadows use independent light projections and the same count-prefixed work contract.
- GPU arguments support multiple rows with X <= 65535. Mesh shaders guard padding groups before emitting output. Preparation checks the native per-axis and total group limits and rejects an unsupported range; it does not truncate or fall back.
- `CookedProgramVersion` and `kMaterialProgramArtifactVersion`: **5 -> 6**. `SceneHostIdentity`: **22 -> 23**. Previous material programs must be recooked; no old-program binding route was added. Meshlet payload schema/profile remain 2.

## Initial frustum gates

GPU-based DX12 validation was enabled. All accepted processes exited normally with code 0.

| Gate | Debug | Release |
|---|---|---|
| MaterialRasterSurfaceProbe build | Passed | Passed |
| `--lx-meshlet-only` | Passed | Passed after native-limit guard |
| `--shadow-decal-only` | Passed | Passed before the final native-limit guard; Debug also covers that guard |
| `--forward-blend` | Passed | Passed before the final native-limit guard; Debug also covers that guard |
| CreatorEditor build and METEOR cutover | Not rerun in this change | Passed |

Camera mode: **24 frames, 67488 checks, 12501 GPU components, validation=0**. Its readback compares the exact GPU ID set with an independent CPU clip replay over captured transformed vertices and packed topology; it also checks duplicates, capacity and the indirect dimensions. Readbacks depend on the producer's current graph versions, not imported version zero.

The real `MeshletFinalizeCS` boundary gate checks survivor counts **0, 1, 65535, 65536 and 131071**. This verifies GPU argument generation; it is not a 131071-meshlet product-scene performance result.

The Release Editor gate passed `dx12.meshletcontract`, `dx12.skinning`, the full render-graph regression, empty HDR capture and live geometry capture: depth/lit pixels **252/252**, indirect mesh submission **1**, Deferred passes **0**, validation problems **0**. The skinning gate reads the new count header and independently audits shadow survivor IDs against posed vertices.

Evidence under `Build/Verification/MeteorCameraMeshlets20261013/`:

- `Debug-Acceptance/verdict.json`: current camera, shadow and blend probes; source stable.
- `Release-Limits/verdict.json`: camera and argument boundaries after the native-limit guard; source stable.
- `Release-Acceptance/verdict.json`: camera, shadow and blend probes before that small guard; source stable.
- `Release-Boundaries/verdict.json`: standalone GPU argument boundary gate.
- `Editor-Release-Acceptance/verdict.json`: current Release Editor product gate; source stable.

Failed runs were reviewed and retained. Corrections covered single-call mesh output counts, reset/cull ID-buffer write versions and UAV barriers, producer-version readback dependencies, the boundary probe's copy argument order, and the Editor skinning probe's previous ID-only list contract.

## Remaining boundaries

- CPU still owns material/geometry bins and PSO submission. Cross-bin native compute dispatch fusion was added in the follow-up below.
- Meshlet-level current-depth HZB is implemented in the B1 follow-up below. History/current-depth two-stage occlusion remains B2.
- Performance improvement and a large product workload are **not accepted** by these correctness gates.
- Vulkan runtime equivalence remains deferred to Phase 4.9.

## B3 follow-up: one native cull dispatch per view

Camera and shadow views now bind structured vertex SRVs and raw topology SRVs in two buffer tables. Each view accepts at most 4096 geometry bins and retains the existing 32 MiB output budget. Unsupported ranges fail preparation; there is no per-bin dispatch fallback.

CPU preparation uploads one range per bin, including its first 64-thread group. The compute shader searches these ordered ranges using the group ID, selects that bin's two buffers, and enumerates its meshlets. Padding lanes and extra groups in the last 2D dispatch row return before reading topology. Buffer selection is uniform within each workgroup. Geometry buffers remain owned by their batches and are read directly; no aggregate geometry copy was added.

Native dispatch count per nonempty culling stage/view is **reset 1 + cull 1 + finalize 1**, independent of bin count within the supported capacity. Three shadow cascades remain three separate views. CPU material/geometry bins and graphics PSO submission are still present, so B3 as a whole remains partial.

RHI now describes structured/raw buffer SRV tables. DX12 validates buffer view ranges before descriptor creation. Its immutable per-recording descriptor page grew from 4096 to 131072 entries so camera/shadow input tables can coexist with transform/material tables. Vulkan descriptor type translation was adjusted, but Vulkan array/runtime equivalence is still a Phase 4.9 task.

`MeshletBatchDispatchProbe.inl` exercises the production reset/cull/finalize shaders with 1, 2 and 4096 bins, different topology sizes, distinct buffer slices, partial/all/zero survivors, padded lanes, and a second compute dispatch row. The largest case has 73727 groups. Every survivor ID and indirect argument is checked. A buffer SRV extending beyond its allocation is rejected before submission. This synthetic kernel gate is correctness evidence, not a 4096-object Editor acceptance or a performance measurement.

The standalone Release batch gate passed with GPU validation on, normal exit 0 and stable source hashes:
`Build/Verification/MeteorMeshletBatch20261013/Release-Batch/verdict.json`.

Follow-up acceptance (all passed, normal exit 0, stable source hashes):

| Gate | Evidence under `Build/Verification/MeteorMeshletBatch20261013/` |
|---|---|
| Release camera, shadow/decal, blend | `Release-Acceptance/verdict.json` |
| Debug camera and native batch boundaries | `Debug-Acceptance/verdict.json` |
| Release Editor meshlet contract, skinning, render graph, empty/live captures | `Editor-Release-Acceptance/verdict.json` |

Both camera probe configurations passed 24 frames, 2265120 checks and 12501 GPU components with validation=0. The new batch checks account for the larger check count. The Release Editor live capture had 252 covered/lit pixels, one mesh indirect submission, zero Deferred passes and zero validation problems; the empty scene retained its METEOR HDR clear. Debug Editor was not rerun. No performance acceptance is claimed.

Per the user's 2026-10-13 instruction, performance measurement and optimization are deferred until all structural implementation is complete. Current gates check build, GPU validation, output correctness and regressions only.
## B1 follow-up: current-frame meshlet HZB

The camera now uses two independent GPU lists. `METEOR.CameraMeshlet.Reset/Cull` emits the frustum list consumed by `METEOR.Scene.OccluderDepth`. One current-frame `GpuGeometryOcclusion::Pyramid` is built from that depth. Both instance visibility and `METEOR.CameraMeshlet.HZB.Reset/Cull` read the same pyramid. The surface mesh shader consumes only the final HZB-compacted IDs and GPU-finalized indirect arguments. The depth producer never consumes the HZB result, so the graph has no feedback dependency.

The HZB kernel constructs each meshlet's world AABB from the actual transformed/posed vertices, then projects its corners conservatively. It retains uncertain/nonfinite bounds, near/W-plane crossings and equal-depth surfaces. It expands the projected rectangle by one source pixel and samples every covered max-depth cell at the selected level (at most 2x2), including odd image extents. The existing frustum and shadow kernels continue to share the same compaction/output contract.

Scene culling requires the explicit versioned graph. The previous unversioned Scene test routes were migrated rather than adding a no-HZB fallback. Test readbacks use producer versions; subsurface and refraction frames expose their current graph inputs for that purpose. The extra HZB table changes the compute root layout, so finalization explicitly rebinds all inputs after its pipeline switch.

This B1 step does not change the cooked meshlet payload or add an old-format reader. The earlier material-program version-6 recook requirement remains. History reprojection, a second recovery pass and temporal invalidation are B2; Vulkan runtime parity remains Phase 4.9. Performance measurement and optimization remain deferred.

### B1 correctness gates

`MeshletHzbProbe.inl` rasterizes a controlled D32 depth image, runs the production HZB builder and meshlet reset/cull/finalize kernels, then checks exact expected ID sets and arguments. Its eight cases cover solid occlusion, a center hole, removal of the occluder, a near/W crossing, invalid projection, camera movement, an edge hole at odd resolution, and the original view again. Front/equal-depth geometry and nonfinite posed vertices are included. Wrong view dimensions, frame/projection identity and previous recordings are rejected. This is a correctness fixture, not a performance workload.

The product Scene probe additionally checks that the final HZB list is a unique subset of the independently replayed frustum list and that its count controls the indirect dimensions. Existing pixel, material, skinning and submission-lifetime checks remain active.

Each of the camera's frustum and HZB stages keeps the cross-bin batch dispatch: one reset, one cull and one finalize per nonempty stage. B1 adds a second stage, not a per-mesh dispatch loop. Shadow views retain their existing frustum stages.

Accepted B1 evidence under `Build/Verification/MeteorMeshletHzb20261013/`:

- `Release-Hzb/verdict.json`: eight native HZB cases, normal exit, validation=0.
- `Release-Final-Hzb/verdict.json`: the same eight cases rerun against the final sources, source stable, normal exit, validation=0.
- `Release-Scene-RG2/verdict.json`: shadow/decal, forward blend, subsurface, refraction, volume, forward transport and Scene integration all passed with GPU validation enabled, source stable and exit code 0. Scene integration covered 59 frames, 25189669 checks and 3901990 GPU components, including the camera list readbacks; unexpected fallbacks=0.
- `Release-Acceptance/runs.json`: camera mode passed 24 frames, 2265686 checks and 12501 GPU components; the subsequent old shadow fixture failed, so this entire run is **not** an accepted aggregate verdict.
- `Debug-Accepted/verdict.json`: the same camera/HZB/batch gates passed, source stable, normal exit, validation=0. Byte-identical shader/fixture copies use isolated generated-source/cache paths; their hashes are recorded.
- `Editor-Release-Acceptance/verdict.json`: meshlet contracts, posed skinning/shadow lists, full render-graph gate and empty/live captures passed. Live coverage/lit pixels: 252/252; indirect mesh submissions: 1; Deferred passes: 0; validation problems: 0. Source stable and normal quit. Debug Editor was not rerun.
- `matched-release-verdict.json`: 24 native matched-image cases passed finite-output and GPU-validation checks; no timing mode or Blender material-quality acceptance was used.

The failed shadow fixture still used a declaration-order graph. Related Scene probes were moved to RG2 with explicit read/write versions. The diagnostic Debug attempt was stopped and retained as a failure. The first isolated Debug fixture lacked its output directory and was rejected before GPU execution; the corrected `Debug-Accepted` run passed. These failures are not counted as successful gates.

After the full gates, only trailing EOF whitespace was normalized; before/after hashes and content-equivalence checks are in `eof-normalization.json`. The final HZB rerun and `git diff --check` passed. No performance acceptance is claimed.

## B2 follow-up: temporal two-stage occlusion

The camera now uses the newest completed, accepted HZB for its first list. Current posed world bounds are projected with that history's view-projection; current-view frustum rejection still uses the current matrix. The first list writes occluder depth. A shared current HZB then tests only meshlets rejected by the first stage. Every first-stage survivor is retained, recovered IDs are appended once, and the final surface consumes this union through GPU-generated indirect arguments. Cold views use current frustum classification in the same first-stage wiring.

Each bin's internal allocation contains a count, capacity for compacted IDs, and one classification word per source meshlet. The mesh shader still reads only the count and compacted-ID prefix. Source flags are overwritten for every valid invocation, including rejected work, and the graph declares the first list as an SRV dependency of recovery. The 32 MiB per-stage budget includes the classification storage. Each nonempty stage retains one reset, one cull and one finalize dispatch across bins.

The final opaque surface depth builds a separate immutable persistent pyramid for the next view. Its graph passes are retained for this external output. History promotion happens only after the exact successful submission passes Scene publication checks; reuse additionally waits for GPU completion. Recording-local readers pin the texture owner and validate device, recording, descriptor and graph identities. Aborted or rejected recordings are not promoted, and an unsubmitted pyramid cannot be imported as history. In-flight ownership remains with submitted frames, so replacing a history entry cannot release textures still in use.

History selection checks scene epoch, view ID, extent, frame order, history revision, Temporal revision/reset, camera-cut revision and lens/projection identity. Temporal camera metadata supplies the unjittered projection for the lens key, while the stored raster view-projection is used for HZB lookup. Ordinary camera movement and posed geometry changes are handled by reprojection and current-frame recovery. If the latest accepted frame is still in flight, the newest completed compatible history is used; this is not a promise of exactly one-frame latency.

This step retains the current depth-first surface renderer: the first raster writes depth, and the final surface shades the union. HW/SW visibility-buffer raster stages and material packet shading remain C2/C4 work. The cooked payload is unchanged, and no compatibility reader or legacy Scene route was introduced.

### B2 correctness gates

The native HZB probe adds eight temporal cases with exact independent first/final ID sets: cold start, removed occluder recovery, retention of first-stage survivors, persistent occlusion, camera movement, lookup through a different previous projection, posed geometry movement, and repeated recovery. It reads back source classifications and bounded indirect arguments. It also rejects unsubmitted history and mismatched extent, scene and frame order. The original eight B1 boundary cases remain active.

The 24-frame camera Scene gate checks actual accepted-history reuse, invalidation after camera cuts/history revision/view changes, the first/final union, independent frustum bounds, existing pixel oracles, and normal GPU retirement. Debug and Release passed with GPU validation enabled, 2266174 checks and 12501 GPU components each. Evidence: `Build/Verification/MeteorMeshletTemporal20261013/Release-Camera/verdict.json` and `Debug-Camera/verdict.json`. The isolated Debug shader/fixture hashes are recorded in `copied-inputs.json`. Release native HZB also passed separately in `Release-Hzb/verdict.json`.

`Editor-Release/verdict.json` passed meshlet contracts, skinning, render-graph checks, and empty/live product captures with normal quit, stable sources, 12 shadow GPU-list checks and 12 payload checks. The live image had 252 covered/lit geometry pixels and zero validation problems; the empty image retained the METEOR HDR clear. Debug Editor and the separate matched-image probe were not rerun for B2.

`Release-Regression/verdict.json` passed all six related modes: shadow/decal, forward blend, subsurface, refraction, volume and forward transport. All gates exited normally with stable source hashes and GPU validation enabled. The aggregate `b2-verdict.json` records the accepted verdict hashes and verification scope; `git diff --check` passed. No performance measurement or optimization was performed. Vulkan runtime equivalence remains Phase 4.9.
