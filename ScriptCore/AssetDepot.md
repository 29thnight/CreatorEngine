# AssetDepot managed CPU bindings

A link is stable identity data; a request and an acquired handle hold native CPU
owners. Nothing serializes a pointer, resident slot or ownership object. Mount
publication remains native-host work. The static supported types are:

| Managed marker | Manifest kind | Actual native CPU owner |
| --- | ---: | --- |
| `Texture` | 3 | `own::shared_owner<const Texture>` |
| `Model` | 1 | `own::shared_owner<const assets::ModelAnimationDescriptor>` |
| `Mesh` | 12 | `own::shared_owner<const assets::ModelMeshDescriptor>` |
| `Skeleton` | 13 | `own::shared_owner<const assets::ModelSkeletonPayload>` |
| `AnimationClip` | 14 | `own::shared_owner<const assets::ModelAnimationPayload>` |
| `ShaderMeta` | 4 | `own::shared_owner<const ShaderMeta>` |
| `MaterialProgram` | 8 | `own::shared_owner<const material_graph::Generation>` |
| `Material` | 2 | `own::shared_owner<const ::Material>` |

`Model` and `Mesh` are small descriptors, not whole-model/geometry bulk owners.
Skeleton and clip requests acquire the selected payload; they do not load sibling
clips. ShaderMeta is authored metadata with a UUIDv4 root (no subasset).
MaterialProgram and Material use UUIDv4/v8 root identities (no subasset).
MaterialProgram is a verified CPU program,
not a compiled GPU pipeline. Material is the Lattice instance-document runtime
view; it is not `experiment::Material` or a legacy reflected material. Unsupported
representations report a failed request rather than silently choosing another
resource shape. Other catalog kinds are not runtime bindings merely because they
have a manifest enum value.

```csharp
[SerializeField]
private AssetLink<Texture> _albedo;

private AssetRequest<Texture>? _request;
private AssetHandle<Texture>? _texture;

// On the game thread. This enqueues native work rather than blocking for I/O.
void BeginLoad()
{
    _request?.Dispose();
    _request = AssetDepot.RequestAsync(_albedo,
        new TextureAssetVariant(TextureAssetColorSpace.Srgb));
}

// Poll from a normal game-thread tick. Do not poll through Task.Run.
void PollLoad()
{
    if (_request is null) { return; }
    AssetRequestSnapshot state = _request.Snapshot();
    if (state.Status == AssetRequestStatus.Ready &&
        _request.TryAcquireResult(out AssetHandle<Texture>? loaded))
    {
        _texture?.Dispose();
        _texture = loaded;
        _request.Dispose(); // the independently acquired result stays alive
        _request = null;
    }
}
```

- `TryAcquire<T>` is an I/O-free resident lookup; a miss returns `false`
- `TextureAssetVariant` is accepted only for Texture; non-default options for any
  other marker fail explicitly on both managed and native boundaries
- `ListRootLinks<T>(mount)` returns identities without loading roots for each registered marker
- `Snapshot()` copies CPU state/error/message and whether the underlying work has
  drained. `Cancelled` can be visible while `IsWorkComplete` is still false
- `TryAcquireResult` returns a fresh independently disposable owner each time it
  succeeds. Dispose every acquired handle. Disposing the request does not release
  those result handles, and cancelling it does not cancel other consumers
- `AssetDepot.Describe(handle)` copies texture dimensions from the pinned CPU
  generation. It does not return pixels, a raw pointer, or a GPU-ready signal
- Handle/request disposal is idempotent. Off-thread disposal and finalizers enqueue
  only a token for a later game-thread release; finalizers never call the renderer
- If an explicit disposal cannot enqueue (including allocation failure), it throws
  with the token retained and disposal retryable; finalization stays enabled.
  Concurrent losing disposal calls do not suppress the winner's finalizer fallback
- Finalizer enqueue failures never escape the finalizer thread. They leave the
  native slot to the explicit CLR session-end sweep, so CPU ownership may remain
  retained until that sweep rather than being reclaimed promptly by GC
- CLR shutdown invalidates and releases every remaining native slot. Its slot
  generations survive reinitialization, and wrapped generations retire their slot
  permanently. A late/stale/type-mismatched release cannot release a new occupant
- Native releases never fetch or recreate DataSystem. GPU allocation completion
  and retirement remain governed by the existing renderer

The serialized link format is
`1:<expected manifest kind>:<canonical UUIDv4/v8 asset ID>:<canonical subasset UUID or nil>`.
The first number is the link format version; kinds are listed above (Texture is 3). A default
link roundtrips as nil/nil. A non-nil subasset requires a non-nil asset. Malformed
or mismatched-kind input preserves the previous field value. Editor commands
report an input error; the inspector validates when Enter is pressed.

The source generator emits direct `AssetLink<T>.TryParse` and `ToString` calls
for the field's exact closed registered type, including aliases such as
`using MeshLink = CreatorEngine.AssetLink<CreatorEngine.Mesh>`. This keeps lazy-link
serialization visible to NativeAOT without reflection or treating a link as a pin.
The inspector/native serialized-field boundary accepts two-digit kinds and still
uses the generated typed parser to preserve the previous value on malformed input.

Opaque token type IDs are separate from manifest kinds. Texture keeps its existing
v34 token ID (3); the additional concrete runtime views use `0x00010001` through
`0x00010007` in the table order after Texture. The request-role bit is `0x80000000`.
Native slots hold a closed variant of the actual typed owner or consumer request.
Every lookup checks index, nonzero generation, role/concrete type and the actual
variant alternative. `experiment::Material`, `ModelAssetGeneration` and geometry
bulk have no registration despite sharing kinds with supported views. Managed
handle/request construction also validates the exact expected token type and role.

Typed request results pin their exact immutable generation across logical unmount
or replacement. New `TryAcquire`/`RequestAsync` calls still consult the current
resolver. Link parsing and root enumeration never imply residency or hard-closure
loading. There are no new non-Texture descriptor-copy/bulk APIs in this ABI slice;
Texture bulk rehydration, managed GPU access and mount authoring remain native work.

The existing API table layout and native/managed version remain 34. No entry,
POD layout or Texture token meaning changed; only the closed dispatch set expanded.
The Program/Material branches require the native material-pipeline runtime slice
(`DataSystem` concrete dispatch and `AssetTypeTraits<material_graph::Generation>`)
to be integrated alongside this boundary change.

## Unrun acceptance fixtures

- `Tools/regression/asset_depot_managed_kind_probe.cpp` is an opt-in dedicated-host
  fixture using the real native registry and DataSystem, not a mock owner. A host
  supplies a source-free v3 mount with all eight roots, calls `Driver::Begin`, ticks
  `Poll` without blocking the GT, then `VerifySessionRestart`. It covers concrete
  type proof, high-bit kind rejection, texture-option rejection, separate consumers,
  independent result owners, wrong-type/role releases, stale tokens and CLR restart
- `Tools/regression/asset_depot_managed_kind_fixture.cs` is an opt-in source-generator
  and managed host fixture. `VerifyStableSerialization` needs no native host and
  verifies all eight lazy closed types, alias registration, nil links, invalid text
  preserving fields, unknown types and non-texture options. `VerifyWarmHostBindings`
  requires all fixture roots already resident and exercises direct NativeAOT-visible
  request/result code with independently disposed owners
- Neither fixture is auto-included in game/runtime targets. No build, test, binary,
  shader or NativeAOT execution was performed. Source inspection is not runtime
  verification. Future authorized runs must also cover allocator-failure injection,
  off-thread Dispose and finalizer queue OOM, forced generation wrap/slot retirement,
  unmount/reload while owners survive, finalizer delivery after restart and renderer
  retirement. Do not claim those acceptance gates have passed from these sources
