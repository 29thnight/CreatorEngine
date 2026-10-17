# Foliage AssetDepot consumer boundary

Implementation scope: Foliage's referenced model, selected static mesh, material,
texture descriptions and actual render geometry/image consumers. The existing
`.foliage` authoring document is still the container; this does not add a cooked
Foliage asset kind or claim source-free loading of that container itself.

## Identity and acquisition

- FoliageType persists `m_modelGuid`, `m_meshAssetId`, and `m_materialAssetId` as
  value IDs. `ModelLink()`, `MeshLink()` and `MaterialLink()` assign the expected
  AssetDepot type. `m_modelName` is a label and old name-only authoring migration
  input, not the runtime identity of a mounted model
- A missing selection is chosen once from the root summary and saved as a stable
  ID. Explicit mesh selection is resolved by ID, never interpreted as mesh zero
- Root acquisition is the existing metadata-only ModelAnimationDescriptor
  request. Only the selected mesh and selected declared Material are requested;
  unrelated meshes, clips, skeletons and material/image payloads remain lazy
- Foliage accepts static meshes and Lattice graph material instances for the
  existing live renderer. Code-only materials produce an explicit
  UnsupportedRepresentation diagnostic. No retired live Code path is restored
- `RequestAsync(modelDescriptor, materialLink)` is a current-preparation
  Loadable lookup, not an old-Material rehydration API. It checks the root's
  declaration, current mount and exact resolver revision, then uses the existing
  material hard-closure worker. Remount before admission/commit is terminal
  Stale; missing current mount is NotMounted. No latest-generation retry occurs
- After successful assembly, the type retains the actual immutable model
  description, selected mesh description and Material/graph instance. These
  owners remain valid after unmount. An old selected mesh can rehydrate its exact
  geometry with the existing owner-based request overload

## Work and lifetime

The component owns independent request handles; FoliageType and its proxy copies
contain no requests. Polling runs before camera/render visibility guards and does
not wait, read files, convert materials, copy embedded pixels, or decode payloads.
Failed, Cancelled and Stale remain terminal until `RebindFoliageType` is called.
Ready replacement owners are published together; previous valid owners remain
usable while a replacement is pending or fails.

Removing a type, replacing the document, uninitializing, or destroying the
component cancels its subscriber handles and drops candidate pins. Accepted work
keeps its captures until the existing DataSystem asset-work completion barrier
has drained. No job captures a FoliageComponent pointer. Bounds-only culling now
runs directly on the owner thread rather than submitting raw component captures
and blocking on their completion; no performance improvement is claimed.

The renderer's existing geometry-demand map now covers selected foliage types
that have instances as well as MeshRenderProxy. Geometry is requested through its
exact descriptor and retained across Pending/zero-budget handoff. It enters the
same CPU-use/frame preparation table already used by mesh uploads and graph
sealing. Graph instance and texture/image retention use the existing frame tables.
Per-instance Foliage DrawSource records carry only type indices, transforms and
bounds. Model/mesh and graph generation owners are deduplicated by the existing
frame identities, not copied into every draw. Native completion tokens, zero-token
quarantine and GPU retirement policy are unchanged.

## Explicit legacy authoring fallback

`m_allowLegacySource` authorizes the existing unmounted source workflow only in
an authoring-enabled host. Old name-only entries migrate once; new typed-only
entries default to no source fallback. Any mounted definition, including a
wrong-kind winner or unsupported representation, remains on the typed error path.

Model source preparation uses the existing tracked preparation with mounted
routing explicitly disabled. Its captured epoch/revision prevents a mount race
from turning this path into an eager full-model scene preparation. Selected
legacy material conversion and embedded texture work run in a tracked asset job.
This transitional route may retain the old whole generation; mounted consumers
never populate that legacy field. Editor model drops now submit this work with
the registered stable model ID instead of importing synchronously by stem.

## Verification status

Only static source inspection and `git diff --check` were performed. No build,
test binary, engine, shader compiler, user computer or publication was run.

Updated, unrun existing fixtures:

- ShaderReflectionSelfTest: persistent typed IDs round-trip while all runtime
  model/mesh/material owners remain absent from serialized data
- VulkanGeometryPassTest: actual foliage draw expansion returns type-index values;
  the retained type snapshot owns resources once, and instance records do not
  prolong resource lifetime after that snapshot is released
- WorkerPoolSelfTest: explicit legacy authoring preparation is polled to a terminal
  status by the harness before culling assertions; the product path never waits

Added source-only integration fixture, not yet registered in a host target:
`Tools/regression/foliage_asset_depot_probe.cpp`. Run its
`RenderTest::RunFoliageAssetDepotProbe` in a dedicated initialized engine test host
with an already-cooked CEMF3 model root containing two distinct static mesh
artifacts and graph materials. It asserts metadata-only mount/root acquisition,
selection of mesh index 1 by ID, zero-cache handoff, no owner-thread payload reads,
no unrelated mesh read, owning proxy handoff after component destruction,
unmount/current-lookup rejection, exact old-geometry rehydration and terminal
failure without automatic retry. It deliberately neither cooks nor compiles a
fixture. A complete runtime pass additionally needs deterministic gates for
remount during read, cancellation while queued, DataSystem shutdown, image
zero-budget upload, foliage-only geometry demand retention across multiple live
render packets, and native zero/completed-token retirement. Those dynamic
cases have not been executed or claimed proven here.

Independent static review found and corrected a missing foliage branch in live
geometry-demand reconciliation. The corrected active set now matches preflight
admission. That review reported no remaining concrete blocker; it is not a
compile or runtime result.
