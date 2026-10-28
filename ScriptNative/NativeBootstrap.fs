namespace CreatorEngine.Native

open System
open System.Runtime.InteropServices
open System.Runtime.CompilerServices
open System.Threading
open CreatorEngine.Native.reflgen_creator_script_native

/// Owns a binding to the native provider for the lifetime of the resident host.
[<AbstractClass; Sealed>]
type NativeBootstrap private () =
    static let gate = obj ()
    static let mutable binding: ScriptApi option = None
    static let mutable ownerThread = 0
    static let mutable fastEntityAlive: EntityAliveCall = null
    static let mutable fastEntityAliveDirect = false

    [<UnmanagedCallersOnly>]
    static member Initialize(address: nativeint, availableBytes: unativeint, fingerprint: uint64, headless: int) : int =
        try
            lock gate (fun () ->
                let thread = Environment.CurrentManagedThreadId
                if ownerThread <> 0 && ownerThread <> thread then
                    -1
                else
                    let required =
                        set [ "Asset_RequestTyped"; "Asset_TryAcquireTyped"; "Camera_NotifyCameraCut"
                              "Simulation_Status"; "Simulation_Submit"; "Simulation_Read"
                              "ExtraComponent_Exists"; "ExtraComponent_GetEnabled"; "ExtraComponent_SetEnabled" ]
                    let candidate =
                        match headless with
                        | 0 -> ScriptApi.Bind(address, availableBytes, fingerprint)
                        | 1 -> ScriptApi.BindWithOptional(address, availableBytes, fingerprint, fun name -> not (Set.contains name required))
                        | _ -> invalidArg "headless" "Invalid native provider mode"
                    let alive, direct = FastNativeCalls.entityAlive candidate.Entity_IsAlive
                    Volatile.Write(&binding, Some candidate)
                    Volatile.Write(&fastEntityAlive, alive)
                    Volatile.Write(&fastEntityAliveDirect, direct)
                    Volatile.Write(&ownerThread, thread)
                    0)
        with _ ->
            -1

    [<UnmanagedCallersOnly>]
    static member Shutdown() : int =
        try
            lock gate (fun () ->
                if ownerThread <> Environment.CurrentManagedThreadId then
                    -1
                else
                    Volatile.Write(&ownerThread, 0)
                    Volatile.Write(&binding, None)
                    Volatile.Write(&fastEntityAlive, null)
                    Volatile.Write(&fastEntityAliveDirect, false)
                    0)
        with _ ->
            -1

    [<UnmanagedCallersOnly>]
    static member EntityIsAlive(index: uint32, generation: uint32) : int =
        try
            lock gate (fun () ->
                match binding with
                | Some api when ownerThread = Environment.CurrentManagedThreadId && not (isNull api.Entity_IsAlive) ->
                    let mutable handle = Unchecked.defaultof<ScriptObjectHandle>
                    handle.index <- index
                    handle.generation <- generation
                    api.Entity_IsAlive.Invoke(handle)
                | _ -> -1)
        with _ ->
            -1

    [<MethodImpl(MethodImplOptions.AggressiveInlining)>]
    static member internal TryGet() =
        if Volatile.Read(&ownerThread) = Environment.CurrentManagedThreadId then
            Volatile.Read(&binding)
        else
            None

    static member internal TryGetForLogging() = Volatile.Read(&binding)

    static member internal UsesDirectEntityAlive = Volatile.Read(&fastEntityAliveDirect)

    [<MethodImpl(MethodImplOptions.AggressiveInlining)>]
    static member internal IsEntityAlive(handle: ScriptObjectHandle) =
        if Volatile.Read(&ownerThread) = Environment.CurrentManagedThreadId then
            let call = Volatile.Read(&fastEntityAlive)
            not (isNull call) && call.Invoke(handle) <> 0
        else
            false

    static member internal IsGameThread =
        let owner = Volatile.Read(&ownerThread)
        owner = 0 || owner = Environment.CurrentManagedThreadId
