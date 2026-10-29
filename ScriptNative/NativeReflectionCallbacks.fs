namespace CreatorEngine

open System
open System.Collections.Generic
open System.Collections.Concurrent
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine.Native
open CreatorEngine.Native.reflgen_creator_script_native

/// The native BlackBoard's stored authoring value kind. Entity and Transform
/// entries contain names/paths, not borrowed object addresses.
type NativeBlackBoardValueKind =
    | None = 0
    | Bool = 1
    | Int = 2
    | Float = 3
    | String = 4
    | Vector2 = 5
    | Vector3 = 6
    | Vector4 = 7
    | Entity = 8
    | Transform = 9

/// An immutable copy taken immediately before a native condition callback.
/// It remains valid after that callback returns and cannot mutate the board.
[<Sealed>]
type NativeBlackBoardSnapshot internal (root: JsonElement) =
    let root = root.Clone()

    let read (key: string) (expected: NativeBlackBoardValueKind) =
        let mutable item = Unchecked.defaultof<JsonElement>
        if not (root.TryGetProperty(key, &item)) then
            raise (KeyNotFoundException($"The native BlackBoard has no key '{key}'."))
        let actual = enum<NativeBlackBoardValueKind>(item.GetProperty("type").GetInt32())
        if actual <> expected then
            invalidArg "key" $"The native BlackBoard key '{key}' is {actual}, expected {expected}."
        item.GetProperty("value")

    member _.Keys = root.EnumerateObject() |> Seq.map _.Name |> Seq.toArray
    member _.HasKey(key: string) =
        let mutable item = Unchecked.defaultof<JsonElement>
        root.TryGetProperty(key, &item)
    member _.GetValueKind(key: string) =
        enum<NativeBlackBoardValueKind>(root.GetProperty(key).GetProperty("type").GetInt32())
    member _.GetValueAsBool(key: string) = (read key NativeBlackBoardValueKind.Bool).GetBoolean()
    member _.GetValueAsInt(key: string) = (read key NativeBlackBoardValueKind.Int).GetInt32()
    member _.GetValueAsFloat(key: string) = (read key NativeBlackBoardValueKind.Float).GetSingle()
    member _.GetValueAsString(key: string) = (read key NativeBlackBoardValueKind.String).GetString()
    member _.GetValueAsVector2(key: string) =
        let value = read key NativeBlackBoardValueKind.Vector2
        Float2(value[0].GetSingle(), value[1].GetSingle())
    member _.GetValueAsVector3(key: string) =
        let value = read key NativeBlackBoardValueKind.Vector3
        Float3(value[0].GetSingle(), value[1].GetSingle(), value[2].GetSingle())
    member _.GetValueAsVector4(key: string) =
        let value = read key NativeBlackBoardValueKind.Vector4
        System.Numerics.Vector4(value[0].GetSingle(), value[1].GetSingle(), value[2].GetSingle(), value[3].GetSingle())
    member _.GetEntityName(key: string) = (read key NativeBlackBoardValueKind.Entity).GetString()
    member _.GetTransformPath(key: string) = (read key NativeBlackBoardValueKind.Transform).GetString()

[<UnmanagedFunctionPointer(CallingConvention.Winapi)>]
type internal NativeCallbackDispatch = delegate of uint64 * int * single * single * nativeint * int -> int

type internal NativeCallbackRoot(root: RootedCallback<NativeCallbackDispatch>) =
    member _.Root = root
    member val Enabled = true with get, set

/// Shared by Loader, component lifetimes, and the public typed registrations.
/// Native disable/drain precedes every GCHandle release. No finalizer guesses
/// that an outstanding native function has stopped using its managed target.
[<AbstractClass; Sealed>]
type internal NativeCallbackRuntime private () =
    static let roots = ConcurrentDictionary<uint64, NativeCallbackRoot>()
    static let mutable generation = 0UL
    static let mutable accepting = true
    static do
        NativeBootstrap.SetCallbackRebindGuard(Func<bool>(fun () -> generation = 0UL && roots.Count = 0))

    static member private Api() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.NativeCallback_BeginGeneration)
                        && not (isNull api.NativeCallback_Register)
                        && not (isNull api.NativeCallback_Disable)
                        && not (isNull api.NativeCallback_TakeRetired)
                        && not (isNull api.NativeCallback_EndGeneration) -> api
        | _ ->
            raise (NativeReflectionException("callback_unavailable",
                "Native callbacks require the initialized API 49 provider on the engine owner thread."))

    static member Drain() =
        // Hosts that never use callbacks need no callback-table invocations.
        if roots.Count <> 0 then
            let api = NativeCallbackRuntime.Api()
            let mutable token = api.NativeCallback_TakeRetired.Invoke()
            while token <> 0UL do
                match roots.TryGetValue(token) with
                | true, entry ->
                    entry.Enabled <- false
                    entry.Root.Dispose()
                    roots.TryRemove(token) |> ignore
                | _ -> ()
                token <- api.NativeCallback_TakeRetired.Invoke()

    static member PrepareGeneration() =
        // Called only after a previous unload barrier has completed, or before
        // the first load. Registration itself starts the native generation.
        accepting <- true

    static member SuspendRegistration() =
        let previous = accepting
        accepting <- false
        { new IDisposable with member _.Dispose() = accepting <- previous }

    static member EndGeneration() =
        if generation <> 0UL then
            let api = NativeCallbackRuntime.Api()
            // Session reset may already have disabled this generation. Its
            // explicit retirement notifications are still required before any
            // root can be released or an obsolete generation can be forgotten.
            NativeCallbackRuntime.Drain()
            let result = api.NativeCallback_EndGeneration.Invoke(generation)
            if result <> 1 && not (result = 0 && roots.Count = 0) then
                raise (NativeReflectionException("callback_busy",
                    "The callback generation cannot unload while a native callback is executing."))
            accepting <- false
            NativeCallbackRuntime.Drain()
            if roots.Count <> 0 then
                raise (NativeReflectionException("callback_retirement",
                    "The native provider did not retire every callback; managed roots remain retained."))
            generation <- 0UL
        else
            accepting <- false

    static member Register(kind: int, invoke: single -> single -> nativeint -> int -> int) =
        let api = NativeCallbackRuntime.Api()
        if not accepting then
            raise (NativeReflectionException("callback_retiring", "The current script generation is unloading."))
        NativeCallbackRuntime.Drain()
        if generation = 0UL then
            let next = api.NativeCallback_BeginGeneration.Invoke()
            if next = 0UL then
                raise (NativeReflectionException("callback_generation", "The native callback generation could not start."))
            generation <- next

        let mutable failure: exn option = None
        let trampoline = NativeCallbackDispatch(fun _ actual x y payload bytes ->
            try
                if actual <> kind || NativeBootstrap.TryGet().IsNone then 0
                else invoke x y payload bytes
            with error ->
                failure <- Some error
                try NativeApi.Log(3, $"[Native callback] {error}") with _ -> ()
                0)
        let root = new RootedCallback<NativeCallbackDispatch>(trampoline, fun () -> failure)
        let token = api.NativeCallback_Register.Invoke(generation, kind, root.Address, 0UL)
        if token = 0UL then
            root.Dispose()
            raise (NativeReflectionException("callback_registration", "The native callback registration was rejected."))
        try
            if not (roots.TryAdd(token, NativeCallbackRoot(root))) then
                invalidOp "The native provider reused a live callback token."
            token
        with _ ->
            // Registration never invokes user code and runs on this thread.
            // A successful disable therefore proves that no call is in flight.
            if api.NativeCallback_Disable.Invoke(token) = 1 then root.Dispose()
            reraise ()

    static member Disable(token: uint64) =
        match roots.TryGetValue(token) with
        | true, entry when entry.Enabled ->
            let api = NativeCallbackRuntime.Api()
            if api.NativeCallback_Disable.Invoke(token) < 0 then
                raise (NativeReflectionException("callback_disable", "The native callback could not be disabled."))
            entry.Enabled <- false
            NativeCallbackRuntime.Drain()
        | true, _ -> NativeCallbackRuntime.Drain()
        | _ -> ()

    static member IsActive(token: uint64) =
        match NativeBootstrap.TryGet() with
        | None -> false
        | Some _ ->
            match roots.TryGetValue(token) with
            | true, entry -> entry.Enabled
            | _ -> false

    static member LastException(token: uint64) =
        NativeCallbackRuntime.Api() |> ignore
        match roots.TryGetValue(token) with
        | true, entry -> entry.Root.LastException
        | _ -> None

/// A game-thread callback registration with a typed native signature. Dispose
/// disables all native copies synchronously; its managed root is freed only
/// after the last in-flight call returns. The owning component also disposes it.
[<Sealed>]
type NativeCallback<'T> internal (owner: Component, token: uint64) as this =
    do owner.Lifetime.RegisterCleanup(Action(fun () -> this.Dispose()))
    member internal _.Token =
        if not (NativeCallbackRuntime.IsActive(token)) then
            raise (ObjectDisposedException("NativeCallback"))
        token
    member _.IsDisposed = not (NativeCallbackRuntime.IsActive(token))
    member _.LastException = NativeCallbackRuntime.LastException(token)
    member _.Dispose() = NativeCallbackRuntime.Disable(token)
    interface IDisposable with member this.Dispose() = this.Dispose()

/// F# functions registered directly with the native callback ABI. Keep a
/// registration while it is installed; early Dispose disconnects its native
/// copies, and component destruction/reload disconnects it automatically.
[<AbstractClass; Sealed>]
type NativeCallbacks private () =
    static member private Create<'T>(owner: Component, kind: int, invoke) =
        if isNull owner then nullArg "owner"
        new NativeCallback<'T>(owner, NativeCallbackRuntime.Register(kind, invoke))

    static member private Text(pointer: nativeint, bytes: int) =
        if bytes < 0 || bytes > 1024 * 1024 || (bytes <> 0 && pointer = 0n) then
            invalidArg "bytes" "Invalid native callback UTF-8 payload."
        if bytes = 0 then String.Empty else Marshal.PtrToStringUTF8(pointer, bytes)

    static member Void(owner: Component, callback: unit -> unit) =
        if isNull (box callback) then nullArg "callback"
        NativeCallbacks.Create<unit>(owner, 0, fun _ _ _ _ -> callback(); 0)

    static member Float(owner: Component, callback: single -> unit) =
        if isNull (box callback) then nullArg "callback"
        NativeCallbacks.Create<single>(owner, 1, fun value _ _ _ -> callback value; 0)

    static member Vector2(owner: Component, callback: Float2 -> unit) =
        if isNull (box callback) then nullArg "callback"
        NativeCallbacks.Create<Float2>(owner, 2, fun x y _ _ -> callback (Float2(x, y)); 0)

    static member Text(owner: Component, callback: string -> unit) =
        if isNull (box callback) then nullArg "callback"
        NativeCallbacks.Create<string>(owner, 3, fun _ _ pointer bytes ->
            callback (NativeCallbacks.Text(pointer, bytes)); 0)

    static member Condition(owner: Component, callback: NativeBlackBoardSnapshot -> bool) =
        if isNull (box callback) then nullArg "callback"
        NativeCallbacks.Create<NativeBlackBoardSnapshot>(owner, 4, fun _ _ pointer bytes ->
            use document = JsonDocument.Parse(NativeCallbacks.Text(pointer, bytes), JsonDocumentOptions(MaxDepth = 8))
            let snapshot = NativeBlackBoardSnapshot(document.RootElement)
            if callback snapshot then 1 else 0)
