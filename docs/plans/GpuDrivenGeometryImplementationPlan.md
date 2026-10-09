# GPU-driven geometry implementation slices

Baseline: `ef829373c41138b8f47dd4011b076e1ec4d5829f` (2026-10-06).
Merged to master by PR #123 (`5ca4e82a`, 2026-10-06). Only the narrow DX12 checks listed in
[merge and first execution](#merge-and-first-execution-2026-10-06) ran; it is not a completed
or broadly runtime-validated feature.

## Goal and boundaries

2026-10-09 lattice follow-up: opaque/masked SceneHost camera/shadow consumers now
have a capability-gated mesh shader route with the existing GPU draw visibility.
Release DX12 TestShadow produced 64 byte-identical indexed/mesh attachment comparisons
and mesh-route GPU validation reported zero problems. See
[implementation and exact acceptance scope](../analysis/LatticeMeshletRendering20261009.md).
Lattice meshlet-level compaction/culling, LOD, broader backend/material acceptance and
performance remain unearned; this does not close GPU-1.

Later on 2026-10-09, LX gained posed-vertex per-meshlet frustum rejection,
CPU selection/submission of authored geometric LODs, independent LOD0 shadow
geometry, current-frame LX depth followed by draw HZB, and cache admission before
topology construction. Debug/Release passed 8 combinations / 192 frames with GPU
validation 0; Release TestShadow had 32 byte-identical attachment comparisons.
GPU LOD selection, visible-meshlet compaction, per-meshlet HZB, light-space LOD,
LOD-scene quality and pressure/performance/Vulkan gates remain unearned. See
[LX integration and evidence](../analysis/LatticeGeometryIntegration20261009.md).

For supported production opaque/masked geometry, the GPU selects visibility and LOD,
compacts bounded work, and produces draw/dispatch arguments. CPU scene publication,
material/PSO bins and command submission remain explicit responsibilities. Existing
indexed rendering remains the capability and authoring-contract fallback. Arbitrary
custom vertex deformation and sorted transparency are not implicitly supported.
No DXR, Work Graphs, shadow-cache resurrection or frame-pacing changes are included.

## Reviewed incremental sequence

1. Build meshlets from the finalized authoring ModelDraft, before CEMC serialization,
   hashing and staging. Store descriptor/bounds, uint32 vertex remap, byte triangle
   indices and source primitive remap in the same immutable model generation.
   Preserve indexed geometry. Version the builder/profile and bind derived data to
   the finalized vertex/index content. Never rebuild model geometry in player cook.
2. Extend indexed GPU visibility coherently to supported skinned geometry in Enhanced
   and LX GBuffer. Carry conservative animated bounds and bone offsets; an instance-ID
   resource declaration alone is not a custom shader semantic contract.
3. Add GPU LOD selection and conservative occlusion as separate graph-owned stages.
   Current depth/history validity must cover camera cuts, resize, device/view/scene
   generations and disocclusion. Invalid/near-plane/uncertain cases remain visible.
4. Add capability-gated mesh shader compilation, pipelines, generation-owned uploads
   and bounded dispatch to both DX12 and Vulkan. Preserve indexed fallback. Disable
   unsafe bind-pose cone tests for skinned/deformed/mirrored/non-uniform geometry.
5. Independently review all producer/consumer ABI, resource lifetime, bounds, barriers,
   counters/overflow and fallback contracts before treating the slices as integrated.

LOD0-only meshlet authoring is infrastructure, not GPU LOD selection. Indexed indirect
is already implemented at the baseline; it is not evidence of mesh shader/HZB support.
The existing design document is a proposal and includes stale pre-indirect observations.

## Authoring and storage contract

- Model identity stays unchanged; no standalone meshlet GUID or global DDC is added
- Editor publishes `Library/ModelAssetGenerations/<ModelId>/<generation>/model.cemc`
- Cook validates/exports that generation to `Derived/Models/<xx>/<ModelId>/<generation>/`
- Meshlets never cross mesh/material or LOD boundaries
- The pinned vcpkg baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`
  selects meshoptimizer 1.2. Its API supports the 64-vertex/126-triangle profile
- Bad or unavailable derived meshlet data must never be interpreted as valid GPU work
- CEMC v11 is the only accepted cooked model format. The v9 (indexed-only) and v10
  (meshlet) readers were removed in `bf3fe4f3`; older generations are rejected as
  "reimport required". No source fallback at runtime or silent replacement of an
  immutable published generation is introduced

## Verification status

Historical (before merge; superseded by the merge section below). Only source inspection and static diff/contract reviews are authorized for this task.
Builds, shader compilation, automated tests, executable probes, GPU captures and runtime
measurements are deliberately unrun. Review findings and these unrun acceptance gates
must accompany each published slice. No merge or runtime verification is implied.

## Initial meshlet authoring usage

Meshlet generation is explicitly opt-in and defaults off for existing and new assets.
At merge, all 11 tracked model sidecars were republished with `buildMeshlets=true` and
`lodLevels=3` (`bf3fe4f3`); skinned meshes skip coarse levels and unreducible meshes keep
LOD0 with a cooker warning.
The choice is persisted as `importSettings.buildMeshlets` in the model sidecar. In the
Content Browser, a model source offers `Model import / Enable meshlets and reimport`
and the corresponding disable action. Both use the game-thread command service and
the same atomic authoring transaction. CLI authoring accepts `--build-meshlets true`
or `false` with `--author-model-asset`; ordinary cook rejects this authoring override.
Reimport without an override retains the persisted setting. Enabling/disabling creates
a new immutable generation; it never patches or silently rebuilds an existing one.
The profile/builder/library versions and all finalized vertex/index bytes are included
in the meshlet digest. Each enabled authoring transaction rebuilds before CEMC hashing,
so source timestamps and stable model identity cannot make old acceleration data fresh.

## Runtime checkpoint 1 (source integrated, execution unverified)

- Exact standard static opaque GBuffer generations can use capability-gated DX12/Vulkan
  mesh pipelines and GPU meshlet frustum compaction with bounded indirect dispatch
- Opt-in static coarse LODs retain the original vertices/indices and material boundary;
  each coarse meshlet primitive remap names that level's own finalized triangle ordinal
- GPU selection uses the stored simplification metric, transformed/projection-scaled to
  a one-pixel threshold. This is not a certified Hausdorff error or measured quality guarantee
- All level frames publish atomically; one unsupported level falls back to indexed LOD0
- Standard skinned Enhanced/LX geometry uses indexed GPU visibility with sealed pose
  validation. Unknown custom deformation stays conservative/direct; custom indexed ID
  indirection requires the explicit versioned ShaderMeta semantic contract
- LX has current-frame HZB from the earlier native depth version. Native HZB is the next
  separate slice; it requires depth and main draws to use the same selected LOD topology
- Shadows retain the original independent caster set and raster path at this checkpoint
- Skinned, masked, custom/generated-special mesh shading/LOD, and sorted transparent
  rendering retain existing paths. No unsafe deformation error or cone test is invented
- The existing debug view reports prepared mesh batches and a fallback diagnostic. These
  are CPU route observations, not GPU-visible counts or performance measurements

Coarse LOD authoring is opt-in (`importSettings.lodLevels`, 0 by default, at most 7).
The Content Browser offers three-level generation/reimport and disabling coarse LODs;
CLI authoring accepts `--lod-levels 0..7`. Positive requests enable meshlets too; disabling
meshlets disables coarse generation. Skinned/unreducible meshes retain LOD0 with authoring
warnings. The default simplifier locks borders and uses direct-from-base reductions.

Static reviews corrected pose overflow and shared-palette mismatches, conservative camera
admission, optional payload range/count amplification, typed PSO binding checks, and
recording-owner teardown. These reviews do not substitute for the deliberately unrun
C++/Slang builds, backend validation, image parity, animation/camera-cut/resize cases or
GPU timing and memory measurements.

## Runtime checkpoint 2 (source integrated, execution unverified)

Native GBuffer now has a separate current-frame D32 occluder pass and one shared
farthest-depth pyramid. Only proved standard opaque depth writers donate depth;
masked/custom/ordered geometry does not silently become an opaque occluder. Depth and
color use the same vertex evaluation and fragment discard logic. For mesh-shader LOD
batches, color visibility filters the exact prior GPU-selected pair list/count, so the
donor silhouette cannot differ because another shader recomputed LOD. The original
GBuffer clear/depth comparison remains unchanged. Invalid bounds, near-plane/W crossings,
nonfinite depth and ambiguous boundary comparisons retain geometry. Nonstandard/custom
pixel-depth semantics bypass occlusion. Missing optional prerequisites restore the whole
view's frustum route; failures after donor recording abort rather than leave phantom depth.

Native directional shadows now compact independent LOD0 caster candidates per cascade
and produce indexed indirect arguments on the GPU. The receiver-cylinder selection,
cascade fitting, alpha coverage, sidedness and independent LX shadow modification remain.
Camera-visible lists/HZB are never used to choose shadow casters. Unproven skin/custom
bounds stay conservative; submitted candidates/bins are labeled separately from unknown
GPU-visible counts. Existing cascade fit behavior for malformed legacy skin is not a
correctness guarantee for unsupported deformation data.

Both production backends establish the existing upload-prefix boundary before preparing
recording-owned visibility and refresh the already-sealed animation palette into that
consuming recording. Native skin shaders bounds-check active bone indices and never fetch
zero-weight sentinel influences. Invalid legacy skin is a compatibility/diagnostic path,
not a new promise of equivalent malformed-data animation.

Coverage limits remain explicit:

- GPU LOD is enabled only for eligible mesh-shader assets/devices; indexed compatibility
  retains GPU instance visibility and LOD0
- Skinned LOD/mesh shading, arbitrary custom deformation, ordered transparency and DXR
  are not implemented by this milestone
- Cone rejection is disabled; nonuniform/mirrored/deformed geometry is never rejected
  using an unsafe bind-pose cone
- The depth prepass itself processes selected geometry. No saved vertex work, speedup,
  memory reduction or visual parity has been measured
- Extreme finite source coordinates may exceed meshoptimizer's useful floating-point
  conditioning before post-build bounds checks; normalized authoring inputs are a future
  robustness extension, not a verified capability of this profile

Completed verification here consists only of independent source-contract reviews,
whitespace/diff checks, and reading project XML/source registrations. No compiler, shader
compiler, tests, executable probe, renderer, capture or GPU timing was run.

## Material coverage extension (current source checkpoint)

Material-based direct submission gates have been removed from accepted native and LX
scene geometry routes. Transparency, refraction, custom/skinned Forward, special captures,
independent shadows, decals and simple world sprites now use GPU-produced arguments on
capable backends while preserving original shaders, order and effects. Unknown semantics
use conservative preserved-stream indirect commands. Hardware compatibility remains.
See [material coverage](../analysis/GpuDrivenMaterialCoverage.md) for exact route semantics,
pre-existing unsupported features, diagnostics and deliberately unrun acceptance.

## Merge and first execution (2026-10-06)

PR #123 merged the six reviewed slices plus `bf3fe4f3` (v9/v10 reader removal and a
meshlet/LOD recook of every tracked model) as `5ca4e82a`. The checks recorded at merge are
the first executed evidence for this milestone:

- `experiment.cooked` 466/466 passed on the v11-only codec
- Editor with the native route forced, DX12: meshlet GBuffer is bit-identical to the indexed
  route on the tested scene, distant geometry switches to coarse LODs, and HZB on/off images
  match. DX12 debug-layer validation errors: 0

Historical red at merge (resolved by the 2026-10-07 fixture repair below): the `dx12.decal` and `dx12.rendergraph` checks drive passes
directly without GPU visibility preparation and end in an exception.

Mapping to the design slices in [GpuDrivenMeshletDxrWiring.md](../design/GpuDrivenMeshletDxrWiring.md#11-구현-슬라이스와-회귀-게이트):

| Slice | Source | Executed evidence | Still open |
|---|---|---|---|
| GD0 | Integrated | DX12 runs the capability-gated path; validation 0 | Unsupported-combination/bad-slice rejection tests; Vulkan run |
| GD1 | Integrated | Bit-identical meshlet vs indexed GBuffer (one scene, DX12) | Depth/coverage/material comparison over the required fixture set |
| GD2 | Integrated | Implied by GD1 image only | CPU oracle visible set, capacity limit/overflow fallback |
| GD3 | Integrated (current-frame HZB) | HZB on/off image identical (static view) | Camera cut, resize, moving occluder, near plane: zero missing geometry |
| HY1 | Partial: static LOD, indexed skinned visibility | Coarse LOD switch observed | LOD error budget, skinned mesh-shader LOD, masked mesh shading, deformation |
| RT0, RT1, HY0 | Not started | none | all |

Material coverage routes (transparent, refraction, special captures, decals, sprites,
independent shadows) have no executed parity check beyond the scenes above.

Next work, in order:

1. Completed 2026-10-07: direct pass fixtures prepare GPU visibility; `dx12.decal` and
   `dx12.rendergraph` pass in Debug/Release with GPU validation 0 and normal exit.
   See [RG5 closure inspection](../analysis/RenderRg5Closure20261007.md). This repairs the
   harness; it does not close GPU-driven material parity or the full RG5 migration
2. Fixture-backed GD2/GD3 acceptance: CPU-oracle visible set, capacity overflow, cut/resize/
   moving occluder, multiple views and two in-flight frames
3. Material route parity for transparency/refraction/decal/sprite/shadow scenes
4. Release performance and memory: CPU prepare/record, cull/bin, prepass and raster p50/p95,
   peak VRAM, upload bytes, against the indexed route on the same binary and assets
5. Vulkan execution belongs to PHASE 4.9 (the editor is DX12-only)
