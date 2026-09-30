# MAT-9 — Scene material performance investigation

2026-09-30. MAT-9 is in progress. This records the model placement regression and
its fixes; the Blender image golden and final tier performance acceptance remain separate gates.

## Reproduction

- Xeon W-2223 (4 cores / 8 threads), RTX 4070 Ti, native DX12 Editor.
- Debug, isolated copy of the model material SoT fixture project, Scene 332×202,
  ImGui scale 0.8 held constant, material editor/preview closed, no Play.
- DX12 validation off for timing. Validation on in the correctness probes below.
- Compare `LX_CookFixture.creator` without Ground/model against CreatorRobot placement.
- Wait for a product frame, warm for three seconds, record eight seconds of continuous
  `.ceprof`, then measure another eight seconds with recording paused.
- FPS comes from the change in `dx12.live.framesRendered` over elapsed wall time.
  Engine frame IDs and the profiler's engine frame duration are not render throughput.
- Builds and GPU correctness probes are excluded from timed windows.

## Causes and fixes

1. Raster used the IBL compute point batch limit. Four model meshes became sixteen
   mesh chunks, multiplying draw calls in shadow, GBuffer, lookup captures and color.
   Scene raster now has a separate, bounded 1,048,576-point ceiling. The compute
   IBL batch retains its original limit, and callers can still request smaller chunks.
2. Every frame allocated and checked a zero LOD array for every source vertex,
   despite cached immutable geometry. Raster builds that transport only on a cache
   miss; perspective pixel UV derivatives still determine texture LOD.
3. Every chunk repacked the same current bone palette. Chunk inputs now retain one
   freshly sealed pose owner, with no reuse of the cached original pose.
4. Render graph culling repeatedly searched every pass and usage for each read.
   Writer indexing preserves all-writer reachability and declaration order.
5. Growing graph arrays repeatedly moved passes containing several Debug STL
   containers. Initial capacities of 128 passes / 256 resources reduce this cost;
   these are capacities, not graph limits.
6. Shadow preparation repeated the material texture/uniform preparation. It now
   shares the current recording's validated packet with its own host pass layout.
   Device, recording, descriptor version and reflected resource layout checks remain.

New CPU markers expose input seal, mesh plan/prepare, graph compile/cull/order,
and each live node's declaration. Runtime marker registration happens when nodes
are added, without registering strings every frame.

## Timing evidence

Mean CPU durations in milliseconds; scopes are inclusive and must not be summed.
Capture boundary fragments are excluded. Both captures are complete with no lost events.

| Debug section | Before, empty | Before, Robot | After, empty | After, Robot |
|---|---:|---:|---:|---:|
| RenderThreadFrame | 7.738 | 21.345 | 6.188 | 14.949 |
| RenderGraphBuild | 3.248 | 7.872 | 2.257 | 4.386 |
| RenderViewCapture | 0.104 | 3.371 | 0.111 | 0.940 |
| MaterialGraphScenePrepare | 0.002 | 2.588 | 0.002 | 2.333 |
| RenderGpuCollect | 1.380 | 3.022 | 1.333 | 2.293 |

Before Robot throughput was approximately 49 rendered FPS; this final run measured
70.13 with recording and 92.48 with recording paused. Empty throughput in the final
run was 161.04 / 195.58 respectively. This is a CPU-limited small viewport reproduction,
not an acceptance result for the user's full-size viewport. Run-to-run host scheduling
variation is visible; the model still adds real rendering and preparation work.

Intermediate measured mesh seal cost fell from 3.35 to 0.86 ms. Graph construction
fell to 3.43 ms in the raster partition measurement. GPU last-sample timings are
not used as an average or a regression ceiling.

Local evidence: `Build/Obj/MaterialProductProbe/Mat9Perf-Debug-before` and
`Mat9Perf-Debug-final`. The final run exited normally and captured a finite product
frame containing four graph material draws, 50 graph passes and no upload/encoder drops.

## Correctness evidence

- Native mesh probe, Debug and Release: 1,117,765 checks, 454,560 GPU components,
  eight vertex masks, three poses, and a single raster chunk containing 8,199 referenced
  vertices. Independent CPU world-space reference matches the GPU result.
- Explicit incomplete LOD input is rejected even after a cache hit. The IBL compute
  limit remains enforced. Pose changes, recording/descriptor invalidation, cache
  ownership and declaration-order culling remain covered.
- Render binding probe, Debug and Release: 179 checks and 64 GPU components.
  Same-recording packet sharing works; stale recordings and missing reflected slots
  are rejected while retaining the previously accepted packet.
- Debug full raster probe: 21,067,374 checks, 3,889,591 GPU components, shared depth,
  skinned occlusion, scene composition, lookup mutation/reuse, texture filtering,
  shadow/decal and generation retirement cases pass. Maximum normalized error
  8.59499e-5; maximum sRGB/filter error 9.56118e-4.
- Release full raster probe passes the same 21,067,374 checks / 3,889,591 GPU components
  and error bounds. Both Editor configurations build successfully.

## Other measured configurations

Each cell is empty → Robot. FPS is completed native render throughput of the hidden
test window, not a monitor presentation rate. These are individual timed runs and
do not establish a repeated-run percentile acceptance ceiling.

| Configuration | Scene / UI scale | Mean render CPU ms | Recording FPS | Paused recording FPS |
|---|---|---:|---:|---:|
| Debug | 1612×796 / 1.5 | 5.934 → 13.094 | 148.01 → 77.22 | 174.42 → 93.26 |
| Release | 332×202 / 0.8 | 0.859 → 1.218 | 1026.76 → 852.59 | 1058.93 → 888.81 |
| Release | 1612×796 / 1.5 | 0.653 → 1.119 | 789.65 → 549.49 | 693.47 → 487.23 |

The large runs requested a 2560×1440 window; recorded Scene and captured attachment
dimensions confirm 1612×796. Both captures contain four material graph draws and
finite attachments, with normal process exit. The runner's warm-upload and GPU-timing
ledger guards passed. Runtime SHA-256 and requested settings are saved per run.
The UI scale changes apply only to each copied test project.

Release still adds about 0.36–0.47 ms of mean render CPU for this model. There is a
measurable cost even where presentation limits may hide its effect. This remains
input to Standard tier acceptance rather than an excuse to close that gate.

These probes compare against independent numerical references. They do not establish
Blender rendered image parity.

## Debug / Release product image comparison

The controlled Robot captures at both 332×202 and 1612×796 have identical camera,
lights, sample/history policy, model/mesh identities and material uniform/texture
inputs. Opaque draw publication order differs; comparison sorts full input records
by identity, preserving every record. All seven attachments (`baseColor`,
`metalRough`, `normal`, `emissive`, `depth`, `preToneHdr`, `display`) match exactly:
maximum absolute difference 0.0 and nonfinite components 0 in both sizes.

Evidence: `Build/Obj/mat9-capture-parity.json` and
`Build/Obj/mat9-capture-large-parity.json`, produced by
`Tools/regression/compare-material-captures.py`. This confirms build-configuration
image consistency for this fixture; Blender and route parity remain open.

## Repeat the measurement

Use an isolated fixture containing `Assets/Models/CreatorRobot.glb` and
`Assets/Scenes/LX_CookFixture.creator`. The runner copies it before making changes.
Every label must be new; existing output is rejected.

```powershell
pwsh -NoProfile -File Tools/regression/measure-material-scene-performance.ps1 `
  -FixtureProject <fixture-project> -Configuration Debug -Label debug-check
python Tools/regression/summarize-material-profile.py `
  Build/Obj/MaterialProductProbe/Mat9Perf-Debug-debug-check/robot.ceprof
```

`-WindowWidth`, `-WindowHeight` and `-UiScale` allow another isolated UI configuration.
The saved viewport/capture dimensions must confirm the actual render size.
The runner checks live renderer progress, GPU timing ledger changes and warm geometry
uploads, and saves an actual product frame through the live HTTP capture service.

## MAT-9 remaining acceptance

- Full-size and moving-camera Standard/Layered/Special timing ceilings, warm/cold
  variant/cache bounds and repeated Debug/Release runs.
- Matched-lighting scene-linear Blender grid/furnace image comparison and
  Deferred/Forward route parity. The fixed EEVEE reference uses an area light;
  it must not be treated as matched lighting for this engine without a defined comparison setup.
- Preserve the measured remaining cost as an open performance gate; Release visibility
  does not close it. ImGui scale origin analysis remains deferred as requested.
