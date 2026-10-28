namespace CreatorEngine.Native

#nowarn "9"

open System
open System.Runtime.InteropServices
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine
open CreatorEngine.Native.reflgen_creator_script_native

type internal NativeServices() =
    let mutable registryRef: Registry option = None
    let contacts = Contacts(fun () -> match registryRef with | Some value -> value.InFixedUpdate | None -> false) :> IContactDispatcher
    let continuations = Continuations()
    let registry = Registry(continuations, contacts)
    let auxiliary = AuxiliaryRuntime()
    let loader = Loader(registry, auxiliary)
    do
        registryRef <- Some registry
        PhysicsComponents.Register()
        CameraBindings.Register()
        LightBindings.Register()
        ExtraBindings.Register()
        ReflectedComponentBindings.Register()
        AnimatorBindings.Register()
        UIBindings.Register()
        MeshBindings.Register()
        SoundBindings.Register()
    member _.Registry = registry
    member _.Contacts = contacts
    member _.Continuations = continuations
    member _.Auxiliary = auxiliary
    member _.Loader = loader

[<AbstractClass; Sealed>]
type RuntimeBootstrap private () =
    static let mutable services: NativeServices option = None
    static member private Report(error: exn) =
        try NativeApi.Log(3, $"[F# runtime] {error}") with _ -> ()
    static member private WriteUtf8(text: string, buffer: nativeint, capacity: int) =
        if buffer = 0n || capacity <= 0 || String.IsNullOrEmpty(text) then 0
        else
            let bytes = Encoding.UTF8.GetBytes(text)
            if bytes.Length > capacity then 0
            else
                Marshal.Copy(bytes, 0, buffer, bytes.Length)
                bytes.Length
    static member private WriteTerminated(text: string, buffer: nativeint, capacity: int) =
        if buffer <> 0n && capacity > 0 then
            let mutable chars = text.Length
            while chars > 0 && Encoding.UTF8.GetByteCount(text.AsSpan(0, chars)) >= capacity do
                chars <- chars * 3 / 4
            let bytes = Encoding.UTF8.GetBytes(text.Substring(0, chars))
            if bytes.Length > 0 then Marshal.Copy(bytes, 0, buffer, bytes.Length)
            Marshal.WriteByte(buffer, bytes.Length, 0uy)
    static member private WithServices(action: NativeServices -> int) =
        try
            match services with
            | Some value when NativeApi.IsGameThread -> action value
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member Initialize() : int =
        try
            if NativeBootstrap.TryGet().IsNone || services.IsSome then -1
            else
                let value = NativeServices()
                try
                    value.Registry.Initialize()
                    value.Continuations.Install()
                    AssetReleaseQueue.Drain()
                    Physics.SetContacts(Some value.Contacts)
                    services <- Some value
                    0
                with _ ->
                    value.Continuations.Uninstall()
                    value.Registry.ReleaseLookup()
                    Physics.SetContacts(None)
                    -1
        with _ -> -1

    [<UnmanagedCallersOnly>]
    static member Shutdown() : int =
        RuntimeBootstrap.WithServices(fun value ->
            let mutable failed = false
            let cleanup action =
                try action ()
                with error ->
                    failed <- true
                    RuntimeBootstrap.Report(error)
            cleanup (fun () -> AssetReleaseQueue.Drain())
            cleanup (fun () -> value.Loader.Unload())
            cleanup (fun () -> value.Registry.Clear())
            cleanup (fun () -> value.Registry.ClearRegistrations())
            cleanup (fun () -> value.Auxiliary.Trees.Clear())
            cleanup (fun () -> value.Auxiliary.Factories.ClearAnimations())
            cleanup (fun () -> value.Auxiliary.Factories.ClearNodes())
            cleanup (fun () -> value.Continuations.DropPending() |> ignore)
            cleanup (fun () -> value.Continuations.Uninstall())
            cleanup (fun () -> value.Registry.ReleaseLookup())
            cleanup (fun () -> Physics.SetContacts(None))
            services <- None
            if failed then -1 else 0)

    [<UnmanagedCallersOnly>]
    static member LoadScripts(path: nativeint) : int =
        RuntimeBootstrap.WithServices(fun value ->
            let text = Marshal.PtrToStringUTF8(path)
            if isNull text || not (value.Loader.Load(text)) then -1 else 0)

    [<UnmanagedCallersOnly>]
    static member ReloadScripts() : int =
        RuntimeBootstrap.WithServices(fun value -> value.Loader.Reload())

    [<UnmanagedCallersOnly>]
    static member IsPreviousContextAlive() : int =
        RuntimeBootstrap.WithServices(fun value -> if value.Loader.IsPreviousContextAlive() then 1 else 0)

    [<UnmanagedCallersOnly>]
    static member InvokeCallable(typeName: nativeint, methodName: nativeint, arguments: nativeint, count: int, output: nativeint, capacity: int) : int =
        try
            match services with
            | Some value when NativeApi.IsGameThread ->
                let decode pointer = let text = Marshal.PtrToStringUTF8(pointer) in if isNull text then String.Empty else text
                let values = if count <= 0 then Array.empty else (decode arguments).Split('\u001f')
                if count > 0 && values.Length <> count then
                    RuntimeBootstrap.WriteTerminated($"Argument count mismatch: expected {count}, received {values.Length}.", output, capacity)
                    int InvokeOutcome.Internal
                else
                    let struct (outcome, payload) = Callable.invoke (value.Loader.LoadedAssemblies()) (decode typeName) (decode methodName) values
                    RuntimeBootstrap.WriteTerminated(payload, output, capacity)
                    int outcome
            | _ -> int InvokeOutcome.Internal
        with error ->
            RuntimeBootstrap.Report(error)
            try RuntimeBootstrap.WriteTerminated($"{error.GetType().Name}: {error.Message}", output, capacity) with _ -> ()
            int InvokeOutcome.Internal

    [<UnmanagedCallersOnly>]
    static member CreateComponent(owner: ScriptObjectHandle, name: nativeint) : int =
        RuntimeBootstrap.WithServices(fun value ->
            let text = Marshal.PtrToStringUTF8(name)
            if isNull text then -1 else value.Registry.Create(ObjectHandle(owner.index, owner.generation), text))

    [<UnmanagedCallersOnly>]
    static member DestroyComponent(instanceId: int) : int =
        RuntimeBootstrap.WithServices(fun value -> if value.Registry.Destroy(instanceId) then 0 else -1)

    [<UnmanagedCallersOnly>]
    static member DispatchLifecycle(instanceId: int, phase: int) : int =
        RuntimeBootstrap.WithServices(fun value -> if value.Registry.DispatchLifecycle(instanceId, phase) then 0 else -1)

    [<UnmanagedCallersOnly>]
    static member SetScriptEnabled(instanceId: int, enabled: int) : int =
        RuntimeBootstrap.WithServices(fun value -> if value.Registry.DispatchEnabled(instanceId, enabled <> 0) then 0 else -1)

    [<UnmanagedCallersOnly>]
    static member FlushRegistrations() : int =
        RuntimeBootstrap.WithServices(fun value -> value.Registry.FlushRegistrations(); value.Registry.ActiveCount)

    [<UnmanagedCallersOnly>]
    static member OnTickSimulate(dt: single) : int =
        try
            match services with
            | Some value when NativeApi.IsGameThread ->
                value.Registry.UpdateTick(dt)
                value.Registry.ActiveCount
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member PrePhysicsTick(dt: single) : int =
        try
            match services with
            | Some value when NativeApi.IsGameThread ->
                AssetReleaseQueue.Drain()
                value.Registry.PrePhysicsTick(dt)
                value.Registry.ActiveCount
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member PostPhysicsTick(dt: single) : int =
        try
            match services with
            | Some value when NativeApi.IsGameThread ->
                AssetReleaseQueue.Drain()
                value.Registry.PostPhysicsTick(dt)
                value.Registry.ActiveCount
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member OnSceneUnload() : int =
        RuntimeBootstrap.WithServices(fun value ->
            value.Registry.FlushRegistrations()
            let swept = value.Registry.SweepOrphans() + value.Auxiliary.Trees.SweepOrphans()
            value.Registry.FlushRegistrations()
            swept)

    [<UnmanagedCallersOnly>]
    static member PublishContacts(events: nativeint, count: int) : int =
        try
            match services with
            | Some value when NativeApi.IsGameThread && count >= 0 && (events <> 0n || count = 0) ->
                let pointer = NativePtr.ofNativeInt<NativeContact>(events)
                for index = 0 to count - 1 do
                    value.Contacts.Route(NativePtr.get pointer index)
                count
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member FlushScriptMessages(messages: nativeint, count: int) : int =
        try
            match services with
            | Some _ when messages = 0n || count <= 0 -> 0
            | Some value when NativeApi.IsGameThread ->
                for index = 0 to count - 1 do
                    let item = messages + nativeint (index * 68)
                    let instance = Marshal.ReadInt32(item)
                    let script = value.Registry.Find(instance)
                    if not (isNull script) && script.IsAlive && script.Enabled then
                        let mutable length = 0
                        while length < 64 && Marshal.ReadByte(item + 4n, length) <> 0uy do
                            length <- length + 1
                        let name = Marshal.PtrToStringUTF8(item + 4n, length)
                        value.Registry.DispatchMessage(script, name)
                count
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member CreateAniBehavior(name: nativeint) : int =
        RuntimeBootstrap.WithServices(fun value ->
            let text = Marshal.PtrToStringUTF8(name)
            value.Auxiliary.Factories.CreateAnimation(if isNull text then String.Empty else text))

    [<UnmanagedCallersOnly>]
    static member DestroyAniBehavior(instanceId: int) : int =
        RuntimeBootstrap.WithServices(fun value -> if value.Auxiliary.Factories.DestroyAnimation(instanceId) then 0 else -1)

    [<UnmanagedCallersOnly>]
    static member HasAniBehavior(name: nativeint) : int =
        try
            match services with
            | Some value ->
                let text = Marshal.PtrToStringUTF8(name)
                if not (isNull text) && value.Auxiliary.Factories.AnimationExists(text) then 1 else 0
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member FlushAniEvents(events: nativeint, count: int) : int =
        try
            match services with
            | Some _ when events = 0n || count <= 0 -> 0
            | Some value when NativeApi.IsGameThread ->
                let pointer = NativePtr.ofNativeInt<AniEvent>(events)
                for index = 0 to count - 1 do
                    value.Auxiliary.Factories.DispatchAnimation(NativePtr.get pointer index)
                count
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member FlushAITicks(ticks: nativeint, count: int) : int =
        try
            match services with
            | Some _ when ticks = 0n || count <= 0 -> 0
            | Some value when NativeApi.IsGameThread ->
                let pointer = NativePtr.ofNativeInt<AITick>(ticks)
                for index = 0 to count - 1 do
                    let item = NativePtr.get pointer index
                    value.Auxiliary.Trees.Tick(item.InstanceId, item.DeltaTime)
                count
            | _ -> -1
        with error ->
            RuntimeBootstrap.Report(error)
            -1

    [<UnmanagedCallersOnly>]
    static member CreateBehaviorTree(owner: ScriptObjectHandle, nodes: nativeint, count: int, entries: nativeint, entryCount: int) : int =
        RuntimeBootstrap.WithServices(fun value ->
            let entity = Entity(ObjectHandle(owner.index, owner.generation))
            let root = NativeBT.build value.Auxiliary.Factories entity nodes count
            let board = NativeBT.loadBoard entries entryCount
            value.Auxiliary.Trees.Add(root, entity, board))

    [<UnmanagedCallersOnly>]
    static member DestroyBehaviorTree(instanceId: int) : int =
        RuntimeBootstrap.WithServices(fun value -> if value.Auxiliary.Trees.Destroy(instanceId) then 0 else -1)

    [<UnmanagedCallersOnly>]
    static member GetBTStats(output: nativeint) : int =
        RuntimeBootstrap.WithServices(fun value ->
            if output = 0n then -1
            else
                NativePtr.write (NativePtr.ofNativeInt<BTStats>(output)) (value.Auxiliary.Trees.GetStats())
                0)

    [<UnmanagedCallersOnly>]
    static member ResetBTStats() : int =
        RuntimeBootstrap.WithServices(fun value -> value.Auxiliary.Trees.ResetStats(); 0)

    [<UnmanagedCallersOnly>]
    static member HasBTNodeType(name: nativeint) : int =
        try
            match services with
            | Some value ->
                let text = Marshal.PtrToStringUTF8(name)
                if not (isNull text) && value.Auxiliary.Factories.NodeExists(text) then 1 else 0
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetAniBehaviorTypeNames(buffer: nativeint, capacity: int) : int =
        try
            match services with
            | Some value -> RuntimeBootstrap.WriteUtf8(String.Join('\n', value.Auxiliary.Factories.AnimationNames), buffer, capacity)
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetBTNodeTypeNames(kind: int, buffer: nativeint, capacity: int) : int =
        try
            match services with
            | Some value -> RuntimeBootstrap.WriteUtf8(String.Join('\n', value.Auxiliary.Factories.NodeNames(enum<BTNodeKind>(kind))), buffer, capacity)
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetComponentTypeNames(buffer: nativeint, capacity: int) : int =
        try
            match services with
            | Some value -> RuntimeBootstrap.WriteUtf8(String.Join('\n', value.Registry.RegisteredTypeNames), buffer, capacity)
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetFieldName(instanceId: int, index: int, buffer: nativeint, capacity: int) : int =
        try
            match services with
            | Some value ->
                let script = value.Registry.Find(instanceId)
                if isNull script then 0 else RuntimeBootstrap.WriteUtf8(script.GetFieldName(index), buffer, capacity)
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetFieldCount(instanceId: int) : int =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then 0 else script.FieldCount
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetFieldType(instanceId: int, index: int) : int =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then 0 else int (script.GetFieldType(index))
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member GetFieldFloat(instanceId: int, index: int) : single =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then 0.0f else script.GetFloat(index)
            | None -> 0.0f
        with _ -> 0.0f

    [<UnmanagedCallersOnly>]
    static member SetFieldFloat(instanceId: int, index: int, fieldValue: single) : unit =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) then script.SetFloat(index, fieldValue)
            | None -> ()
        with _ -> ()

    [<UnmanagedCallersOnly>]
    static member GetFieldInt32(instanceId: int, index: int) : int =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then 0 else script.GetInt32(index)
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member SetFieldInt32(instanceId: int, index: int, fieldValue: int) : unit =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) then script.SetInt32(index, fieldValue)
            | None -> ()
        with _ -> ()

    [<UnmanagedCallersOnly>]
    static member GetFieldBool(instanceId: int, index: int) : int =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) && script.GetBool(index) then 1 else 0
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member SetFieldBool(instanceId: int, index: int, fieldValue: int) : unit =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) then script.SetBool(index, fieldValue <> 0)
            | None -> ()
        with _ -> ()

    [<UnmanagedCallersOnly>]
    static member GetFieldString(instanceId: int, index: int, buffer: nativeint, capacity: int) : int =
        try
            match services with
            | Some value ->
                let script = value.Registry.Find(instanceId)
                if isNull script then 0 else RuntimeBootstrap.WriteUtf8(script.GetString(index), buffer, capacity)
            | None -> 0
        with _ -> 0

    [<UnmanagedCallersOnly>]
    static member SetFieldString(instanceId: int, index: int, text: nativeint) : unit =
        try
            match services with
            | Some value ->
                let script = value.Registry.Find(instanceId)
                if not (isNull script) then
                    let decoded = Marshal.PtrToStringUTF8(text)
                    script.SetString(index, if isNull decoded then String.Empty else decoded)
            | None -> ()
        with _ -> ()

    [<UnmanagedCallersOnly>]
    static member GetFieldFloat2(instanceId: int, index: int) : Float2 =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then Float2.Zero else script.GetFloat2(index)
            | None -> Float2.Zero
        with _ -> Float2.Zero

    [<UnmanagedCallersOnly>]
    static member SetFieldFloat2(instanceId: int, index: int, fieldValue: Float2) : unit =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) then script.SetFloat2(index, fieldValue)
            | None -> ()
        with _ -> ()

    [<UnmanagedCallersOnly>]
    static member GetFieldFloat3(instanceId: int, index: int) : Float3 =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then Float3.Zero else script.GetFloat3(index)
            | None -> Float3.Zero
        with _ -> Float3.Zero

    [<UnmanagedCallersOnly>]
    static member SetFieldFloat3(instanceId: int, index: int, fieldValue: Float3) : unit =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) then script.SetFloat3(index, fieldValue)
            | None -> ()
        with _ -> ()

    [<UnmanagedCallersOnly>]
    static member GetFieldObject(instanceId: int, index: int) : ScriptObjectHandle =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if isNull script then Unchecked.defaultof<ScriptObjectHandle> else script.GetObject(index).Handle.ToNative()
            | None -> Unchecked.defaultof<ScriptObjectHandle>
        with _ -> Unchecked.defaultof<ScriptObjectHandle>

    [<UnmanagedCallersOnly>]
    static member SetFieldObject(instanceId: int, index: int, fieldValue: ScriptObjectHandle) : unit =
        try
            match services with
            | Some value -> let script = value.Registry.Find(instanceId) in if not (isNull script) then script.SetObject(index, Entity(ObjectHandle(fieldValue.index, fieldValue.generation)))
            | None -> ()
        with _ -> ()
