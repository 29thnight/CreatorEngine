open System
open System.Runtime.InteropServices
open System.Reflection
open System.Collections.Generic
open System.IO
open System.Runtime.CompilerServices
open CreatorEngine
open CreatorEngine.Native
open CreatorEngine.Native.reflgen_creator_script_native

type ProbeAction(initial: NodeStatus) =
    inherit ActionNode()
    member val Status = initial with get, set
    member val Calls = 0 with get, set
    override this.Tick(_, _) =
        this.Calls <- this.Calls + 1
        this.Status

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeTable()

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeHeadlessTable()

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeMissingSlot()

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeMissingRequiredSlot()

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern uint64 reflgen_creator_script_native_abi_fingerprint()

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeInitialize(nativeint entry, nativeint table, unativeint size, uint64 fingerprint, int headless)

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeShutdown(nativeint entry)

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeEntityIsAlive(nativeint entry, uint32 index, uint32 generation)

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeAssetReleases()

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeLogFile()
[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeLogMember()
[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeLogLine()

let check condition message =
    if not condition then failwith message

let verifyLink<'T> (assetId: AssetId) (expected: AssetKind) =
    let value = AssetLink<'T>(assetId)
    let mutable parsed = Unchecked.defaultof<AssetLink<'T>>
    check (value.ExpectedKind = expected && value.IsValid && AssetLink<'T>.TryParse(value.ToString(), &parsed) && parsed.Equals(value)) "asset kind and stable identity round trip"

let reject action message =
    let mutable rejected = false
    try action () with :? ArgumentException -> rejected <- true
    check rejected message

[<MethodImpl(MethodImplOptions.NoInlining)>]
let private isRegistered (registry: Registry) instance =
    not (isNull (registry.Find(instance)))

[<MethodImpl(MethodImplOptions.NoInlining)>]
let private conditionWorks (auxiliary: AuxiliaryRuntime) =
    let board = BlackBoard()
    board.SetBool("ready", true)
    auxiliary.Factories.CreateNode("NativeConditionProbe").Tick(0.1f, board) = NodeStatus.Success

[<MethodImpl(MethodImplOptions.NoInlining)>]
let private measureContacts (contacts: IContactDispatcher) (stream: ContactStream) (contact: NativeContact) =
    let before = GC.GetAllocatedBytesForCurrentThread()
    let mutable index = 0
    while index < 10000 do
        contacts.Route(contact)
        if stream.Read().Length <> 1 then failwith "contact was lost"
        contacts.EndFrame()
        index <- index + 1
    GC.GetAllocatedBytesForCurrentThread() - before

[<EntryPoint>]
let main _ =
    check (Marshal.SizeOf<PhysicsBodyState>() = 60 && Marshal.SizeOf<PhysicsShapeState>() = 88) "physics body and shape ABI sizes"
    check (Marshal.SizeOf<CharacterMovementState>() = 88 && Marshal.OffsetOf<CharacterMovementState>("Tick").ToInt32() = 56) "character ABI size and tick offset"
    check (Marshal.SizeOf<PhysicsHit>() = 64 && Marshal.SizeOf<PhysicsQueryRequest>() = 48 && Marshal.SizeOf<PhysicsBatchResult>() = 16) "physics query ABI sizes"
    check (Marshal.SizeOf<SimulationStatus>() = 32 && Marshal.SizeOf<SimulationSnapshot>() = 32 && Marshal.SizeOf<GcStats>() = 48) "simulation and GC ABI sizes"
    let table = ProbeTable()
    let size = unativeint ScriptApiSize
    let fingerprint = reflgen_creator_script_native_abi_fingerprint()
    let getEntry name =
        typeof<NativeBootstrap>.GetMethod(name, BindingFlags.Public ||| BindingFlags.Static).MethodHandle.GetFunctionPointer()
    let initialize = getEntry "Initialize"
    let shutdown = getEntry "Shutdown"
    let isAlive = getEntry "EntityIsAlive"
    check (ProbeInitialize(initialize, table, size, fingerprint ^^^ 1UL, 0) = -1) "native initialization rejects wrong fingerprint"
    check (ProbeInitialize(initialize, table, size, fingerprint, 0) = 0) "native invokes F# initialization"
    check (NativeBootstrap.UsesDirectEntityAlive = System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported) "direct native entity call selected when dynamic code is supported"
    check (System.Threading.Tasks.Task.Run(fun () -> ProbeInitialize(initialize, table, size, fingerprint, 0)).Result = -1) "worker initialization rejected"
    check (ProbeEntityIsAlive(isAlive, 7u, 3u) = 1) "native calls F# which calls native table"
    CreatorEngine.Scripts.NativeCallableProbe.Log()
    check ((Marshal.PtrToStringUTF8(ProbeLogFile())).EndsWith("NativeBehaviorProbe.fs") && ProbeLogLine() > 0 && Marshal.PtrToStringUTF8(ProbeLogMember()) = "Log") "F# log preserves caller location"
    let assetId = AssetId.Parse("276fc6b7-8304-4f68-a750-2672355e0867")
    let namedVariant = CreatorEngine.Scripts.NativeConstructorProbe.TextureVariant()
    check (namedVariant.Role = 1u && namedVariant.ColorSpace = TextureAssetColorSpace.Source && not namedVariant.Compress) "named optional texture variant preserves defaults"
    verifyLink<Texture> assetId AssetKind.Texture
    verifyLink<Model> assetId AssetKind.Model
    verifyLink<Mesh> assetId AssetKind.Mesh
    verifyLink<Skeleton> assetId AssetKind.Skeleton
    verifyLink<AnimationClip> assetId AssetKind.AnimationClip
    verifyLink<ShaderMeta> assetId AssetKind.ShaderMeta
    verifyLink<MaterialProgram> assetId AssetKind.MaterialProgram
    verifyLink<Material> assetId AssetKind.Material
    verifyLink<CodeMaterialProgram> assetId AssetKind.MaterialProgram
    verifyLink<AuthoredMaterial> assetId AssetKind.Material
    let link = AssetLink<Texture>(assetId)
    check (link.IsValid && AssetLink<Texture>.Parse(link.ToString()).Equals(link)) "typed asset identity round trip"
    let beforeReleases = ProbeAssetReleases()
    let request = AssetDepot.RequestAsync(link)
    let snapshot = request.Snapshot()
    check (snapshot.Status = AssetRequestStatus.Ready && snapshot.Message = "ready" && snapshot.IsWorkComplete) "native asset request snapshot"
    let mutable resultAsset: AssetHandle<Texture> = null
    check (request.TryAcquireResult(&resultAsset)) "native request result acquisition"
    let descriptor = AssetDepot.Describe(resultAsset)
    check (descriptor.Width = 64u && descriptor.Height = 32u && descriptor.MipLevels = 3u) "native texture descriptor"
    resultAsset.Dispose()
    request.Cancel()
    let mutable asset: AssetHandle<Texture> = null
    check (AssetDepot.TryAcquire(link, &asset) && not asset.IsDisposed) "typed native asset acquisition"
    System.Threading.Tasks.Task.Run(fun () -> asset.Dispose()).Wait()
    check (asset.IsDisposed && ProbeAssetReleases() = beforeReleases + 1) "worker disposal is queued"
    AssetReleaseQueue.Drain()
    check (ProbeAssetReleases() = beforeReleases + 2) "queued release runs on game thread"
    request.Dispose()
    check (ProbeAssetReleases() = beforeReleases + 3) "request release runs on game thread"
    let entity = Entity(ObjectHandle(7u, 3u))
    check entity.IsAlive "F# entity resolves native handle"
    UIBindings.Register()
    CameraBindings.Register()
    ExtraBindings.Register()
    let bone = entity.GetComponent<BoneComponent>()
    check (not (isNull bone) && not bone.Pinned && bone.ResolvedBoneIndex = 17) "native bone fields read"
    bone.Pinned <- true
    check bone.Pinned "native bone pinned round trip"
    check (isNull (Entity(ObjectHandle(7u, 4u)).GetComponent<BoneComponent>())) "native bone stale handle"
    let decal = entity.GetComponent<DecalComponent>()
    check (not (isNull decal)) "native decal lookup"
    decal.SliceX <- 4u
    decal.SliceY <- 3u
    decal.SliceNumber <- 7
    decal.UseAnimation <- true
    decal.IsLoop <- true
    decal.SlicePerSeconds <- 2.5f
    decal.Timer <- 0.25f
    check (decal.SliceX = 4u && decal.SliceY = 3u && decal.SliceNumber = 7 && decal.UseAnimation && decal.IsLoop && decal.SlicePerSeconds = 2.5f && decal.Timer = 0.25f) "native decal field round trip"
    let image = entity.GetComponent<ImageComponent>()
    check (not (isNull image)) "native image lookup"
    image.Color <- Color4(0.2f, 0.3f, 0.4f, 0.5f)
    check (image.Color.R = 0.2f && image.Color.A = 0.5f) "native image color round trip"
    check (entity.GetComponent<CameraComponent>().FieldOfView = 65.0f) "native camera property"
    check (isNull (Entity(ObjectHandle(7u, 4u)).GetComponent<ImageComponent>())) "native image stale handle"
    check (Input.GetKeyDown(KeyCode.A) && Input.GetKey(KeyCode.A)) "key down counts as pressed"
    check (Input.GetKey(KeyCode.B) && not (Input.GetKeyDown(KeyCode.B))) "held key remains pressed"
    check (Input.GetKeyUp(KeyCode.C) && not (Input.GetKey(KeyCode.C))) "released key is not pressed"
    let mutable audioSettings = AudioPlaySettings()
    check (audioSettings.Volume = 1.0f && audioSettings.Pitch = 1.0f && audioSettings.Priority = 128 && audioSettings.Bus = AudioBus.SFX) "F# audio default constructor preserves native defaults"
    let zeroSettings = Unchecked.defaultof<AudioPlaySettings>
    check (zeroSettings.Pitch = 0.0f) "defaultof audio settings remains zeroed"
    audioSettings.SpatialBlend <- 0.0f
    let audioSource = AudioSource(AudioAssetId.Parse("276fc6b7-8304-4f68-a750-2672355e0867"))
    let playback = Audio.Play2D(Audio.World, audioSource, audioSettings)
    check (playback.IsValid && playback.IsPlaying) "F# audio play uses initialized settings"
    playback.Pause()
    check (playback.State = AudioPlaybackState.Paused) "F# audio pause"
    playback.Resume()
    playback.Stop()
    let mutable completion = Unchecked.defaultof<AudioPlaybackCompletion>
    check (Audio.TryDequeueCompletion(&completion) && completion.Playback = playback && completion.Reason = AudioPlaybackEndReason.Stopped) "F# audio completion"
    let transform = entity.GetComponent<Transform>()
    check (not (isNull transform)) "F# transform wrapper exists"
    transform.LocalPosition <- Float3(1.0f, 2.0f, 3.0f)
    transform.Translate(Float3(0.5f, 1.0f, 2.0f))
    let moved = transform.LocalPosition
    check (moved.X = 1.5f && moved.Y = 3.0f && moved.Z = 5.0f) "F# transform wrapper updates native state"
    check (isNull (Entity(ObjectHandle(7u, 4u)).GetComponent<Transform>())) "missing transform returns null"
    let factories = Dictionary<string, Func<Component>>()
    let callbacks = Dictionary<string, ScriptLifecycle>()
    CreatorEngine.Generated.ScriptRegistry.RegisterAll(Action<string, Func<Component>>(fun name factory -> factories.Add(name, factory)))
    CreatorEngine.Generated.ScriptRegistry.RegisterAllLifecycle(Action<string, ScriptLifecycle>(fun name lifecycle -> callbacks.Add(name, lifecycle)))
    let assetScript = factories["NativeAssetProbe"].Invoke()
    check (assetScript.GetFieldType(0) = FieldType.AssetLink) "generated typed asset field kind"
    assetScript.SetString(0, link.ToString())
    check (assetScript.GetString(0) = link.ToString()) "generated typed asset field serialization"
    let script = factories["FSharpNativeMover"].Invoke()
    script.BindEntity(entity)
    check (script.FieldCount = 1 && script.GetFieldType(0) = FieldType.Float) "generated serialized field contract"
    script.SetFloat(0, 4.0f)
    check (script.GetFloat(0) = 4.0f) "generated field accessors"
    callbacks["FSharpNativeMover"].OnBeginSimulation.Invoke(script)
    callbacks["FSharpNativeMover"].OnTickSimulate.Invoke(script, 0.5f)
    let scriptedPosition = transform.LocalPosition
    check (scriptedPosition.X = 3.5f && scriptedPosition.Y = 3.0f && scriptedPosition.Z = 5.0f) "generated lifecycle drives native Transform"
    let mutable activeRegistry: Registry option = None
    let contacts = Contacts(fun () -> match activeRegistry with | Some value -> value.InFixedUpdate | None -> false) :> IContactDispatcher
    let registry = Registry(Continuations(), contacts)
    activeRegistry <- Some registry
    registry.Register("FSharpNativeMover", factories["FSharpNativeMover"])
    registry.RegisterLifecycle("FSharpNativeMover", callbacks["FSharpNativeMover"])
    let instance = registry.Create(ObjectHandle(7u, 3u), "FSharpNativeMover")
    let registered = registry.Find(instance)
    check (obj.ReferenceEquals(entity.GetComponent<CreatorEngine.Scripts.FSharpNativeMover>(), registered)) "managed component lookup uses registry"
    registered.SetFloat(0, 6.0f)
    check (registry.DispatchLifecycle(instance, 0)) "registry initializes generated script"
    check (registry.DispatchLifecycle(instance, 1)) "registry enters scene"
    check (registry.DispatchLifecycle(instance, 2)) "registry begins simulation"
    let stream = contacts.Observe(registered, ObjectHandle(7u, 3u), ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4, ContactGrouping.ShapePairs)
    let mutable first = Unchecked.defaultof<ContactEndpoint>
    first.Owner <- ObjectHandle(7u, 3u)
    first.Component <- 11UL
    first.Shape <- 1u
    first.Role <- ShapeRoles.Attack.Id
    let mutable second = Unchecked.defaultof<ContactEndpoint>
    second.Owner <- ObjectHandle(8u, 1u)
    second.Component <- 22UL
    second.Shape <- 2u
    second.Role <- ShapeRoles.Hurt.Id
    let mutable contact = Unchecked.defaultof<NativeContact>
    contact.First <- first
    contact.Second <- second
    contact.Tick <- 100UL
    contacts.Route(contact)
    let delivered = stream.Read()
    check (delivered.Length = 1 && not delivered[0].OtherEntity.IsAlive) "F# contact routing and orientation"
    contacts.EndFrame()
    check (stream.Read().IsEmpty) "F# contact frame clears"
    for _ = 1 to 2000 do
        contacts.Route(contact)
        contacts.EndFrame()
    let firstAllocation = measureContacts contacts stream contact
    let allocated = measureContacts contacts stream contact
    check (firstAllocation = 0L) $"F# contact first measurement allocations: {firstAllocation}"
    check (allocated = 0L) $"F# contact steady-state allocations: {allocated}"
    for _ = 1 to 5 do contacts.Route(contact)
    check (stream.Overflowed && stream.RequiredCapacity = 5) "bounded contact buffer reports overflow"
    let mutable overflowRejected = false
    try stream.Read().Length |> ignore with :? InvalidOperationException -> overflowRejected <- true
    check overflowRejected "overflow refuses partial contact results"
    contacts.EndFrame()
    stream.Dispose()
    let grouped = contacts.Observe(registered, ObjectHandle(7u, 3u), ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4, ContactGrouping.SensorTargets)
    contact.Kind <- 3
    contact.Tick <- 101UL
    contacts.Route(contact)
    check (grouped.Read().Length = 1 && grouped.Read().[0].Phase = ContactPhases.Begin) "sensor overlap begins"
    contact.Second.Shape <- 3u
    contacts.Route(contact)
    check (grouped.Read().Length = 1) "same target emits once per tick"
    contacts.EndFrame()
    contact.Kind <- 4
    contact.Tick <- 102UL
    contacts.Route(contact)
    check (grouped.Read().IsEmpty) "one remaining shape keeps grouped target active"
    contact.Second.Shape <- 2u
    contacts.Route(contact)
    check (grouped.Read().Length = 1 && grouped.Read().[0].Phase = ContactPhases.End) "last shape ends grouped target"
    contacts.EndFrame()
    registry.UpdateTick(0.5f)
    check (transform.LocalPosition.X = 6.5f) "F# registry dispatches generated fixed tick"
    check (registry.Destroy(instance)) "registry destroys script"
    registry.FlushRegistrations()
    check grouped.Disposed "script lifetime disposes contact stream"
    check (registry.ActiveCount = 0) "registry removes destroyed script"
    check (obj.ReferenceEquals(entity.GetComponent<CreatorEngine.Scripts.FSharpNativeMover>(), null)) "destroyed managed component is not returned"
    registry.ClearRegistrations()
    let fixturePath = typeof<CreatorEngine.Scripts.FSharpNativeMover>.Assembly.Location
    let copyDir = Path.Combine(Path.GetTempPath(), "creator-fsharp-loader-" + Guid.NewGuid().ToString("N"))
    Directory.CreateDirectory(copyDir) |> ignore
    let copyPath = Path.Combine(copyDir, "ScriptNativeGeneratorProbe.dll")
    File.Copy(fixturePath, copyPath)
    File.Copy(typeof<list<int>>.Assembly.Location, Path.Combine(copyDir, "FSharp.Core.dll"))
    let auxiliary = AuxiliaryRuntime()
    let loader = Loader(registry, auxiliary)
    check (loader.Load(copyPath)) "collectible script load succeeds"
    check (auxiliary.Factories.AnimationExists("NativeAnimationProbe")) "generated animation factory is registered"
    check (auxiliary.Factories.NodeExists("NativeConditionProbe")) "generated behavior factory is registered"
    check (conditionWorks auxiliary) "F# behavior node reads F# blackboard"
    let sequence = SequenceNode()
    let firstNode = ProbeAction(NodeStatus.Success)
    let secondNode = ProbeAction(NodeStatus.Running)
    sequence.AddChild(firstNode)
    sequence.AddChild(secondNode)
    let board = BlackBoard()
    check (sequence.Tick(0.1f, board) = NodeStatus.Running) "F# sequence retains running child"
    secondNode.Status <- NodeStatus.Success
    check (sequence.Tick(0.1f, board) = NodeStatus.Success && firstNode.Calls = 1) "F# sequence resumes without replaying earlier nodes"
    let loadedInstance = registry.Create(ObjectHandle(7u, 3u), "FSharpNativeMover")
    check (loadedInstance > 0) "collectible generated factory is registered"
    check (registry.DispatchLifecycle(loadedInstance, 0)) "loaded script initializes"
    File.WriteAllBytes(copyPath, [| 0uy; 1uy; 2uy |])
    check (loader.Reload() = 1 && isRegistered registry loadedInstance) "failed reload preserves existing instance"
    File.Copy(fixturePath, copyPath, true)
    check (loader.Reload() = 0 && not (isRegistered registry loadedInstance)) "successful reload retires old instance"
    check (not (loader.IsPreviousContextAlive())) "retired collectible context is reclaimed"
    for _ in 1 .. 5 do
        check (loader.Reload() = 0) "repeated reload succeeds"
        check (not (loader.IsPreviousContextAlive())) "repeated reload reclaims retired collectible context"
    loader.Unload()
    check (System.Threading.Tasks.Task.Run(fun () -> ProbeEntityIsAlive(isAlive, 7u, 3u)).Result = -1) "worker thread rejected"
    check (ProbeInitialize(initialize, table, size, fingerprint ^^^ 1UL, 0) = -1) "failed rebind rejected"
    check (ProbeEntityIsAlive(isAlive, 7u, 3u) = 1) "failed rebind preserves previous binding"
    reject (fun () -> ScriptApi.Bind(table, 7un, fingerprint) |> ignore) "truncated table"
    reject (fun () -> ScriptApi.Bind(table, size, fingerprint ^^^ 1UL) |> ignore) "incorrect fingerprint"
    let api = ScriptApi.Bind(table, size, fingerprint)
    let mutable owner = Unchecked.defaultof<ScriptObjectHandle>
    owner.index <- 7u
    owner.generation <- 3u
    check (api.Entity_IsAlive.Invoke(owner) = 1) "valid handle"
    owner.generation <- 4u
    check (api.Entity_IsAlive.Invoke(owner) = 0) "stale handle"
    let mutable position = Unchecked.defaultof<ce_script_vector3>
    position.x <- 1.25f
    position.y <- -2.5f
    position.z <- 3.75f
    api.Transform_SetLocalPosition.Invoke(owner, position)
    let result = api.Transform_GetLocalPosition.Invoke(owner)
    check (result.x = position.x && result.y = position.y && result.z = position.z) "aggregate argument and return"
    ProbeMissingSlot()
    reject (fun () -> ScriptApi.Bind(table, size, fingerprint) |> ignore) "missing required slot"
    check (ProbeInitialize(initialize, table, size, fingerprint, 0) = -1) "normal provider rejects absent slot"
    let partial = ScriptApi.BindWithOptional(table, size, fingerprint, fun name -> name = "Entity_IsAlive")
    check (isNull partial.Entity_IsAlive) "optional slot remains absent"
    check (ProbeInitialize(initialize, table, size, fingerprint, 1) = 0) "headless provider accepts optional slot"
    ProbeMissingRequiredSlot()
    check (ProbeInitialize(initialize, table, size, fingerprint, 1) = -1) "headless provider rejects missing required slot"
    let headlessTable = ProbeHeadlessTable()
    check (ProbeInitialize(initialize, headlessTable, size, fingerprint, 1) = 0) "headless sparse table initializes"
    check (ProbeEntityIsAlive(isAlive, 7u, 3u) = 1) "headless supported entry remains callable"
    check (ProbeShutdown(shutdown) = 0) "native invokes F# shutdown"
    check (ProbeEntityIsAlive(isAlive, 7u, 3u) = -1) "shutdown revokes binding"
    printfn "SCRIPT_NATIVE_TABLE_OK version=%d entries=212" ScriptApiVersion
    0
