# AssetDepot managed Texture slice

The managed API supports `Texture` only. A link is stable identity data; a request
and an acquired handle hold native CPU owners. Nothing serializes a pointer,
resident slot or ownership object. Mount publication remains native-host work.

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
- `ListRootLinks<Texture>(mount)` returns identities without loading roots
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
`1:3:<canonical UUIDv4/v8 asset ID>:<canonical subasset UUID or nil>`.
The first number is the link format version; `3` is the Texture kind. A default
link roundtrips as nil/nil. A non-nil subasset requires a non-nil asset. Malformed
or mismatched-kind input preserves the previous field value. Editor commands
report an input error; the inspector validates when Enter is pressed.

The source generator emits direct `AssetLink<Texture>.TryParse` and `ToString`
calls in the existing static field dispatch. This keeps lazy-link serialization
and its closed supported type visible to NativeAOT, without reflective type lookup
or treating a link as a resident pin. Other managed runtime kinds, Texture bulk
rehydration, GPU access, and managed mount authoring are not implemented here.

The API table was appended and its native/managed version is 34. Source inspection
is not a successful runtime or NativeAOT verification: build, managed ABI smoke,
GC/finalizer, reload, concurrent cancellation and shutdown/reinitialize execution
remain required before claiming those acceptance checks passed.
