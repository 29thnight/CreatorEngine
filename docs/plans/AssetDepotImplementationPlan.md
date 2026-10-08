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
- `build-asset-set` source-recooks TextureSourceImage and selected Model descriptor,
  Skeleton and AnimationClip artifacts. Import roots and external buffer inputs are
  captured immutably and hashed. Selected child build identities do not depend on
  unrelated siblings. Verified CAS reuse and transactional unique output directories
  remain independent of Player compilation. The OS owns the cache lock; interrupted
  candidates are never accepted as published output.
- DataSystem exposes `MountAssetSet`, `UnmountAssetSet`, `ListRootLinks<T>`, typed
  `RequestAsync<T>` and I/O-free `TryAcquire<T>` for Texture and granular animation
  descriptors/skeletons/clips. Requests have independent cancellation, resolver-bound
  keys, stale publication gates and bounded weak-current/retained caches.
- A model descriptor reads no skeleton/clip files. It captures ordered child identity
  and locator metadata, including absent Loadable children. Only a selected child
  worker opens and verifies its payload; absent captured children fail without resolving
  a newer mount. Acquired origins retain exact per-artifact backing, not every file in
  the mount. Current unique immutable outputs have no physical garbage collector;
  a future collector requires an artifact-store/root lease. External file replacement
  or removal can therefore cause an I/O or integrity failure before first acquisition.
- Animator owns only selected/current/transition/layer clips and their skeletons
  through scheduled evaluation. Legacy geometry uses an explicit full skin-binding
  compatibility gate before accepting a granular palette. This is not yet standalone
  granular mesh rendering.
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
- The managed Texture adapter uses generation-checked native owner tokens, independent
  request/result handles, deterministic Dispose and POD-only deferred finalizer release.
  Explicit static type registration and AssetLink authoring serialization support
  NativeAOT without reflecting over native owners.
- Reflection/authoring serializers understand own shared/exclusive owners. Deserialization
  builds a candidate before replacement; immutable reflected payloads are copied for
  legacy mutable serialization hooks and shown read-only in the editor. Nested
  exclusive reflected fields are rejected safely; immutable polymorphic Component
  lists require a dedicated const-safe serializer. Resource copy contracts still
  govern unreflected state and isolation of shared mutable children.

## Remaining integration and support boundaries

- Standalone mesh artifacts, node/mesh descriptor metadata and MeshRenderer/RHI typed
  geometry acquisition remain to be implemented. CEMCv11 whole-model geometry is a
  transitional adapter, not the finished fine-grained model format.
- Texture descriptors still retain CodecImage and embedded-image bulk. Independently
  evictable image payloads, exact rehydration and cross-ID blob decode sharing remain.
- Some legacy Material/Texture/ShaderMeta APIs are synchronous transitional loaders;
  full typed asynchronous acquisition and independent cooked producers for every
  supported resource kind are not complete.
- Source-free Player/content cutover, managed support beyond Texture, and removal of
  superseded compatibility paths remain separate implementation slices.
- Cache budgets describe retained cache pins, not allocator or GPU memory immediately
  freed. Existing consumers, in-flight jobs and accepted frames intentionally outlive
  eviction and logical unmount.

## Invariants

- `AssetLink<T>` stores stable asset/subasset identity and expected type, not residency.
- `AssetRequest<T>` is a consumer request; joined worker lifetime/cancellation is separate.
- Runtime-bound keys include resolver revision or closure digest, not only parent bytes.
- Mount/root enumeration does not read or decode root payloads.
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
