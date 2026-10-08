# AssetDepot implementation

Baseline: CreatorEngine `50eff63b31eba108a48adadc115a7f564635a40e`.
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
- `build-asset-set` produces source-recooked TextureSourceImage artifacts only. It
  includes both hard/loadable edges, verifies immutable CAS reuse and publishes a
  new output directory transactionally. The OS owns the cache lock so forced
  cancellation does not permanently block later builds. Incomplete work directories
  are never reused as published output.
- DataSystem exposes `MountAssetSet`, `UnmountAssetSet`, `ListRootLinks<T>`,
  `RequestAsync<Texture>` and I/O-free `TryAcquire<Texture>`. Texture requests have
  independent cancellation, exact backing/dependency pins, stale publication gates,
  and a default 64 MiB conservative retained-graph charge budget.
- All accepted DataSystem preparation/bundle/prewarm jobs share one shutdown barrier.
  Scheduler terminal observers also finish requests whose dependent body never runs.
- Model generations and graph/LX instances use actual own owners through their
  consumers. Model cache retention defaults to 256 MiB; graph retention to 128 MiB.
  These are explicit cache-pin budgets, not estimates of allocator/GPU bytes freed.
  Model pins are deduplicated per sealed scene input; draw records keep indices.
- Runtime material clones no longer silently publish themselves into Materials.

The Texture descriptor still pins its CodecImage; models still use whole-model
aggregate storage. Granular mesh/skeleton/clip/image artifacts, independently evictable
bulk, cross-ID blob decode sharing, typed model acquisition and the remaining
Material/Texture/ShaderMeta/UI/Terrain ownership migration are not complete. Managed
asset handles and independent Player/content cutover are separate slices in progress.

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
