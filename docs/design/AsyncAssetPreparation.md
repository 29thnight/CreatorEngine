# CPU asset preparation and scene publication

The editor Open Scene action queues `scene.open_async`. Its command result means
`queued: true`, not scene completion or GPU readiness. Existing `scene.load` and
`scene.switch` retain their synchronous command contracts.

## Preparation boundary

`SceneManager` parses the document with the common job scheduler. `DataSystem`
walks the current scene and referenced material/prefab documents, including
encoded prefab overrides, inline material instances, bundle entries, named
sprite/UI textures, and foliage model references. A first open does not require
a previously written `ScenePreload` list.

1. Workers validate/decode immutable model candidates
2. The scene owner publishes completed model generations and discovers their
   generated material graph identities
3. Workers prepare graph programs, validation bytes and SHA-256 digests; the
   owner publishes those exact immutable results
4. Workers prepare the required CPU material/texture owners, using the existing
   material codecs and texture variant rules
5. The scene owner constructs entities and activates the prepared scene at the
   existing scene structure boundary

Scene loading and editor model placement share model preparation tickets.
Model placement keeps its existing incremental owner-thread application budget.
Graph preparation uses `GenerationStore::BeginPreparation`, `Prepare`, and
`Publish`; expensive compilation/verification does not hold the store mutex.
Requests for one asset revision share a candidate. An invalidated revision cannot
publish over the current owner, and a failure preserves the accepted generation.

The explicit synchronous `PrewarmSceneMaterials` path also retains generations.
It no longer discards a cooked program merely to populate the disk cache.
Subsequent entity loading consumes the prepared owner without decoding that
cache a second time. Generated compiler inputs are content-keyed and atomically
published so overlapping requests cannot observe another writer's truncation.

## Lifetime and scheduling

Preparation uses existing `job_handle` dependency chains. Asset lanes are bounded
by compiler capacity and leave two workers unreserved where the pool size allows.
A normal frame pump polls completion before reading a result; only explicit
synchronous/lifecycle APIs wait for incomplete jobs. Cancellation prevents scene
publication and does not attempt to preempt a compiler/decoder already running.

The request index is weak, so it does not keep another permanent copy of resident
model/graph owners. Superseded work remains drainable until its callbacks finish.
Asset changes invalidate request epochs; CPU resource cache insertion validates
its source epoch and is blocked across the cache invalidation boundary.
Shutdown drains accepted work before clearing catalogs and caches.

Texture and Material runtime identities use one out-of-line atomic allocator.
They are nonzero, never recycled, and fail closed at exhaustion. Persisted
FileGuid/model identities and content fingerprints are unchanged. Material copy
and resource move identity behavior is unchanged.

## Readiness and remaining synchronous work

Progress counts CPU preparation items within the displayed stage, including
instance recipes; it is not a count of unique source assets. Dependency discovery
has no known total. Shader progress counts live compiler requests rather than
letting one completed request hide others.

CPU preparation is distinct from GPU readiness. Texture upload, fences, resource
retirement, PSO creation, and render-thread scene lookup initialization retain
their existing ownership and readiness contracts. Entity construction remains on
the scene owner and may still take a long frame. A small shared worker pool can
also delay other jobs while non-preemptible decoding/compilation is running.

Player startup, explicit synchronous scene commands, watcher hot-reload
acceptance, and Material Graph authoring apply/preview compilation remain
synchronous entry points. This change does not claim zero game-thread stalls or
move mixed RHI initialization onto arbitrary workers.

## Verification scope

The patch was reviewed statically. Added regression source covers prepared-owner
reuse, same-revision coalescing, dirty reload retry, late completion rejection,
unchanged-product identity, removal and shutdown. Builds, regression execution,
engine execution, and performance measurements are still required before a
runtime/performance claim.
