# AssetDepot implementation

Design source baseline: CreatorEngine `50eff63b31eba108a48adadc115a7f564635a40e`.
The feature branch also integrates upstream master
`4602aff1c469b8d4af8a18a178b70f3e9ed3c37a` without rewriting either history.
Its graph-only live path, material recovery boundaries and RG7 APIs are retained;
new acceptance notifications distinguish accepted work from abortable preparation.
Completion-zero retirement remains quarantined until explicit GPU idle.
Ownership dependency: `ownership_cpp` `5889a32c58a49fcfded3a9d61484867849cc7dbb`.

This is an incremental implementation of the approved asset-management and ownership
design. A foundation slice is not completion of the runtime migration.

## Slice order

1. Pin the public ownership library and use a single engine include boundary.
2. Add typed non-owning links and CEMF v3 AssetSet manifests. Preserve the legacy v2
   reader/writer; a new AssetSet requires source cooking, never implicit edge conversion.
3. Extend the existing catalog with immutable multi-mount resolver snapshots, explicit
   conflict/override validation, typed roots and hard-closure diagnostics.
4. Close asset-job shutdown gaps before exposing new asynchronous acquisition paths.
5. Add independent texture-first `BuildAssetSet` / `build-asset-set`, then extend the
   supported producer/loader matrix deliberately.
6. Connect typed consumer requests, bounded retained caches plus weak live lookup,
   exact-artifact pins and stale/cancellation publication gates.
7. Migrate actual model/material/texture/shader/graph consumers and frame pins; split
   independently rehydratable geometry/image/skeleton/clip bulk payloads.
8. Complete source-free Player/content compatibility, managed native-owner handles,
   NativeAOT registration and removal of temporary legacy lifetime paths.

## Implemented source slices (2026-10-08)

- The pinned ownership dependency, CEMF v3 link/manifest schema, immutable catalog
  transactions and generic loose/pak range sources are present.
- `build-asset-set` source-recooks TextureSourceImage, authored ShaderMeta, Lattice
  Material and verified MaterialProgram documents, plus selected Model descriptor,
  Mesh, Skeleton, AnimationClip, model Material and embedded Texture artifacts. Import roots and external buffer inputs are
  captured immutably and hashed. Selected child build identities do not depend on
  unrelated siblings. Verified CAS reuse and transactional unique output directories
  remain independent of Player compilation. The OS owns the cache lock; interrupted
  candidates are never accepted as published output.
- DataSystem exposes `MountAssetSet`, `UnmountAssetSet`, `ListRootLinks<T>`, typed
  `RequestAsync<T>` and I/O-free `TryAcquire<T>` for Texture and granular animation
  descriptors/skeletons/clips, plus small Mesh descriptors and exact geometry payloads.
  Requests have independent cancellation, resolver-bound
  keys, stale publication gates and bounded weak-current/retained caches.
- A schema4 model descriptor reads no mesh/skeleton/clip files. It captures ordered child identity
  and locator metadata, including absent Loadable children. Only a selected child
  worker opens and verifies its payload; absent captured children fail without resolving
  a newer mount. Acquired origins retain exact per-artifact backing, not every file in
  the mount. Managed immutable output roots now carry cooperative OS store guards,
  including unselected lazy backing. Existing/extracted unenrolled roots remain
  readable but noncollectible; external replacement can still report an integrity/I/O
  failure and is never treated as permission to resolve a newer generation.
- Independent CEGE mesh artifacts omit logical identity/name/material fields. Their
  bounded codec validates the packed rendering contract and full skin binding; optional
  invalid meshlet/LOD data falls back to indexed geometry. Earlier model descriptor
  schemas require recook into schema4, which includes explicit Loadable Material edges
  and the captured collider policy beside ordered mesh/node/material summaries.
- MeshRenderer recognizes mounted typed meshes before legacy paths. Components,
  proxies and durable frame tables retain small descriptors; a separate 128 MiB raw
  geometry cache shares compatible reads/decodes across logical IDs. Each descriptor
  preserves its own logical closure and a matching verified backing source/locator.
  Descriptor retention defaults to 8 MiB and conservatively includes skeleton pins.
- Pending geometry requests survive frame handoff; unused/hidden demands are pruned.
  GPU-resident raster paths need no CPU payload. The live graph path does read/copy
  geometry on CPU, so it explicitly retains transient payload owners until all selected
  views finish. Existing graph-derived CPU copies have their own bounded ownership.
- Mounted Texture descriptors retain metadata, an exact image recipe and backing,
  not decoded pixels. Independent CodecImage requests share compatible verified reads
  and decodes; descriptor and image caches have separate weak lookup/retained budgets.
  Sampling-only labels share bytes, while compression/filtering/mip recipes distinguish
  different decoded results. The source-image representation still decodes on first
  descriptor acquisition to derive its metadata; this is not a metadata-only file format.
- DX12/Vulkan uploads, material graph bindings, sprites/UI/decals, gizmos, environment
  work and editor previews now use explicit image owners. CPU admission precedes native
  recording; deferred views retain exact results across mesh/image/program misses and
  credit limits. No-demand frames cancel their subscribers; accepted native resources
  keep the existing GPU retirement path. Scene and bundle results carry zero-budget
  image handoffs through construction and current camera admission.
- Animator owns only selected/current/transition/layer clips and skeletons through
  scheduled evaluation. Both granular and transitional legacy geometry require the
  full compatible skin-binding contract before accepting its current palette.
- Explicit mounted-model placement prepares schema4 hierarchy, Mesh descriptors,
  Material templates and Skeleton from one resolver snapshot through existing model
  tickets. Child owners travel in a construction input value packet; clips stay lazy.
  Source-node anchors apply transforms once, including meshless roots and multi-mesh
  siblings. Components receive descriptor/Material/Animator owners without source or
  legacy whole-model fallback. Collider preparation accepts CookedDefault, Enabled or
  Disabled before work admission. Policy-specific tickets do not supersede each other;
  optional raw geometry gates skip disabled/uninstantiated collision demand. Cold mesh
  descriptors can still decode their own source artifact to derive metadata. Collision
  triangles are copied before scene mutation, published through native physics ownership,
  and released after handoff; repeated meshes reuse the same geometry publication key.
  Animated static-triangle colliders fail closed.
- Scene preparation pins selected mesh descriptors through construction. The public
  completion pump is nonblocking `PollSceneLoads`; canceled tickets finish without
  waiting for shared mesh jobs, which still participate in service shutdown drain.
  Terminal Stale does not implicitly retry the latest mount.
- All accepted DataSystem preparation/bundle/prewarm jobs share one shutdown barrier.
  Scheduler terminal observers finish requests whose dependent body never runs.
  Bundle and scene preparation results carry real resource owners through consumption.
- Model, Material, Texture, ShaderMeta and graph/LX consumers use actual own owners.
  Cache entries combine weak current lookup with explicit bounded strong retention.
  Mutable material instances remain separate from immutable published templates;
  publication severs mutable aliases and clones do not silently register themselves.
- Components, editor previews and accepted rendering inputs retain their own resources.
  Model, texture and graph-instance frame tables deduplicate exact stable identities;
  draw records carry indices/anchored borrows. Native GPU completion, abort and
  completion-zero quarantine paths remain responsible for physical retirement.
- Source definitions may declare Internal or External placement explicitly (omitted
  scope remains Internal). External references retain exact type and Hard/Loadable
  semantics without importing the other set; the mounted union validates their graph.
- Typed `RequestAsync<material_graph::Generation>` and `RequestAsync<Material>` load
  verified CPU programs and Lattice runtime templates through a fixed same-snapshot
  Texture → Program → Material job DAG. Separate concrete stores prevent manifest-kind
  aliasing. Default/override texture recipes retain color-space and full-mip policy;
  their real descriptor/hard-closure charges count toward bounded retention. Authored
  `experiment::Material` is not falsely advertised as the same concrete runtime type.
- Packages can attach independently built immutable AssetSets via an explicit
  directory list and host ABI token. Receipts/CAS bytes are reverified after copy;
  native preflight checks the complete typed union. Startup publishes all configured
  sets atomically before consumers run, including acyclic asset graphs whose set-level
  dependency ordering has a cycle. Runtime activation reads no root payloads.
- Authored ShaderMeta has a v3 fresh-source CEDO producer and typed asynchronous
  source-free descriptor loader. Captured source/sidecar bytes must match the product;
  the immutable result retains only its exact artifact source. This is metadata
  readiness, not HLSL compilation or GPU program/PSO readiness. Material dependency
  continuations can request Texture against the same admitted resolver snapshot.
- Managed bindings cover Texture, Model/Mesh descriptors, Skeleton, AnimationClip,
  ShaderMeta, MaterialProgram and the Lattice Material runtime view. Closed native
  alternatives hold actual typed owners/requests; concrete token type proofs are
  distinct from manifest kinds. Generation/session tombstones, deterministic Dispose
  and POD-only deferred finalizer release remain. Static closed-type registration and
  AssetLink authoring serialization include aliases/two-digit kinds for NativeAOT;
  the v34 function table/POD ABI is unchanged.
- Reflection/authoring serializers understand own shared/exclusive owners. Deserialization
  builds a candidate before replacement; immutable reflected payloads are copied for
  legacy mutable serialization hooks and shown read-only in the editor. Nested
  exclusive reflected fields are rejected safely; immutable polymorphic Component
  lists require a dedicated const-safe serializer. Resource copy contracts still
  govern unreflected state and isolation of shared mutable children.

## Remaining integration and support boundaries

- Granular rendering and source-free mounted-model placement use the typed hierarchy,
  independent Lattice materials/embedded textures and exact collider preparation.
  Saved-scene inline material restoration and authored/code material views are the next
  integration slice. Other legacy aggregate embedded texture consumers remain adapters.
  Automatic set activation is connected to the existing package bootstrap, which still
  requires the legacy scene/audio/source-identity closure; a v3-only package is pending. CEMCv11 remains a transitional adapter for other legacy consumers.
- Legacy source/generated Texture adapters explicitly retain non-rehydratable images.
  In particular, legacy material property/name loads and embedded ModelTextureAsset
  pixel/subresource storage are not yet fully cut over to standalone image recipes.
  Mounted typed graph/UI/sprite/generic paths use the independent image store.
- Synchronous scene entry points reject a composite Pending bundle rather than waiting
  for image payload jobs on the caller. Cold mounted-content activation must use the
  asynchronous scene preparation path. Player startup now polls that path, commits
  activation/play before opening its command service, and excludes preparation time
  from its first simulation delta. Legacy synchronous command callers still need
  cutover; no synchronous readiness guarantee is claimed for that bridge.
- BuildAssetSet now checks immutable import receipts before invoking source producers.
  A hit revalidates the complete captured file inventory, recipe/tool/target/schema
  identity and every typed CAS product, then bypasses that importer/converter. Rejected
  candidate captures do not contaminate later inputs. Reused-import and recooked-import
  metrics are distinct from reused blobs; no warm-build timing has been measured.
- Shared store guards protect mounted/retired roots, narrowed artifacts and pak
  archives. A held exclusive capability plus exact backing-incarnation revalidation
  is required for cooperative deletion/repack eligibility. No collector or destructive
  operation runs automatically; logical unmount/eviction never delete storage.
  Newly copied outputs need fresh prepublication enrollment; extracted unenrolled
  package content remains explicitly noncollectible.
- Some legacy Material/Texture/ShaderMeta APIs are synchronous transitional loaders;
  full typed asynchronous acquisition and independent cooked producers for every
  supported resource kind are not complete.
- General source-free Player/content cutover and removal of superseded compatibility
  paths remain separate implementation slices. Managed/NativeAOT execution and all
  source-free runtime acceptance gates remain unrun, including the expanded kinds.
- Cache budgets describe retained cache pins, not allocator or GPU memory immediately
  freed. Existing consumers, in-flight jobs and accepted frames intentionally outlive
  eviction and logical unmount.

## Invariants

- `AssetLink<T>` stores stable asset/subasset identity and expected type, not residency.
- `AssetRequest<T>` is a consumer request; joined worker lifetime/cancellation is separate.
- Runtime-bound keys include resolver revision or closure digest, not only parent bytes.
- Mount/root enumeration does not read or decode root payloads.
- Direct/configured runtime mount rejects unsupported installed kind/representation/
  schema pairs before publication. Generic catalog candidate builders stay format-
  neutral; future declared kinds do not imply an installed runtime loader.
- Unmount blocks new resolution and advances resolver revision. Dependent acquisition
  may fail until its external hard dependency is mounted again. Existing consumers keep
  their exact old snapshot and artifact backing. Unmount does not delete storage.
- Strong ownership cycles fail with a dependency path. Loadable links remain values.
- Cache eviction releases only cache-owned references. Components, jobs and accepted
  frames retain their own pins; reference counts are not GPU-completion evidence.
- New requests stop before draining every DataSystem-capturing job outside locks.
- Native COM/SDK destruction and the existing GPU completion/quarantine path remain.

## Verification boundary

Only source inspection, textual/static checks and repository metadata inspection are
permitted for this implementation session. Builds, tests, engine/tool execution,
benchmarks and shader compilation have not been run. Regression sources may be added,
but their presence must not be reported as a passing runtime result.

Before merge, authorized validation must cover malformed/legacy manifests, deterministic
serialization, external missing/type/cycle errors, explicit overrides, source-free texture
loads, consumer cancellation versus shared work, stale publication, logical unmount,
old-generation rehydration, bounded retention, shutdown races and GPU failure/retirement.
