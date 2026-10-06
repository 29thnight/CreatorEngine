# GPU-driven geometry: static review checkpoint

Baseline: `ef829373c41138b8f47dd4011b076e1ec4d5829f`.

## Integrated source paths

- Opt-in meshlets/LOD are built before CEMC hashing/staging. CEMC v11 retains explicit
  v9/v10 readers, and model generation publication remains atomic
- DX12/Vulkan expose optional mesh pipelines, capability limits, generation-owned uploads,
  typed dispatch checks and completion-owned resource retirement
- Standard static opaque GBuffer uses GPU meshlet visibility, projected-error LOD and
  indirect mesh dispatch; other supported geometry retains indexed paths
- Native current-frame HZB has matching opaque donor topology, shared fragment discard,
  separate depth and a shared max pyramid. Recheck filters actual prior GPU-selected work
- LX consumes its earlier native depth version. Native shadow visibility uses independent
  per-cascade receiver cylinders and indexed indirect commands, never camera HZB

## Reviewed invariants and corrected findings

- Stream ownership, overflow/count limits, exact triangle corner/primitive mapping,
  geometry/settings digests, conservative spheres and disabled unsafe cone rejection
- Coarse indices reference base-used vertices; original index arrays remain unchanged
- Invalid optional runtime data has observable indexed fallback; staged new generations
  and cook export reject derived-data decode warnings
- Shader ID-indirection semantics belong to immutable shader generations, not layout IDs
- Current pose palettes, active bone ranges, zero-weight sentinels, double-precision
  bounds and conservative unknown-input handling
- Vertex/index/meshlet states, indirect barriers, optional Vulkan entry points, PSO kinds,
  compiler profiles, rollback and retirement
- Both production paths establish the upload-prefix boundary before recording-owned
  palette/visibility preparation, including CPU-only/custom fallback views
- Failures after donor depth recording abort rather than leave phantom occluders
- UI counters distinguish prepared/submitted work from unknown GPU-visible counts
- Editor controls use queued game-thread commands, native/UTF-8 paths and the same
  identity-preserving transaction as CLI authoring

## Deliberately unrun acceptance

No C++ build, shader compilation, tests, executable probes, renderer, captures, image
comparisons or GPU measurements were run. Source review is not proof of executable
correctness, visual parity, performance or hardware compatibility. Project XML/source
registrations and whitespace/diff checks were inspected only.

Future authorized execution should cover both backends, capability fallback, old CEMC,
malformed acceleration data, cuts/resize/multiple views, skin animation, masked coverage,
mixed indexed/mesh batches, LOD transitions, shadow cascades, upload/submission failure
and resource pressure.

## Explicit coverage limits

GPU LOD is limited to eligible mesh-shader assets/devices. Indexed fallback uses GPU
instance visibility with LOD0. Skinned mesh shading/LOD, arbitrary custom deformation,
ordered transparency, DXR and cone culling are outside this checkpoint. Malformed legacy
skin fit and extreme-coordinate meshoptimizer conditioning are not new correctness
promises. Extra prepass/compute/allocation costs and any benefit remain unmeasured.

## Material coverage extension

The initial checkpoint's transparency/custom direct-route limits are superseded by the
[material coverage extension](GpuDrivenMaterialCoverage.md). Accepted materials retain
original shaders/effects on GPU-produced indexed/nonindexed indirect paths. Generic custom
streams are preserved rather than compacted; CPU global ordering and submission remain.
Mesh-stage/LOD restrictions and hardware compatibility are separate from material routing.
The same no-build/no-test/no-runtime verification limit applies.
