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
