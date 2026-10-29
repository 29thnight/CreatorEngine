module NativeRuntimeRegression

open System
open System.Runtime.InteropServices
open CreatorEngine

[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReplaceBody(uint64 identity, single velocity)
[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern single ProbeBodyVelocity()
[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeBodyRole()
[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeSetOwnerAlive(int alive)
[<DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeErrorCount()

[<AllowNullLiteral>]
type private TickComponent() =
    inherit Component()
    member val Action: Action<TickComponent, single> = null with get, set
    member val End: Action = null with get, set
    member val Calls = 0 with get, set
    member val Begins = 0 with get, set
    member val Ends = 0 with get, set
    member val Uninitialized = 0 with get, set
    member val Thread = 0 with get, set
    member val Dt = 0.0f with get, set
    member val FailInit = false with get, set
    member val FailBegin = false with get, set
    member this.OnInitialized() = if this.FailInit then invalidOp "init injection"
    member this.OnBeginSimulation() =
        this.Begins <- this.Begins + 1
        if this.FailBegin then invalidOp "begin injection"
    member this.OnEndSimulation() =
        this.Ends <- this.Ends + 1
        if not (isNull this.End) then this.End.Invoke()
    member this.OnUninitializing() = this.Uninitialized <- this.Uninitialized + 1
    member this.OnTickSimulate(dt: single) =
        this.Calls <- this.Calls + 1
        this.Dt <- dt
        this.Thread <- Environment.CurrentManagedThreadId
        if not (isNull this.Action) then this.Action.Invoke(this, dt)

let runFixedUpdate() =
    let mutable checks = 0
    let check value message =
        if not value then invalidOp message
        checks <- checks + 1
    let mutable active: Registry option = None
    let contacts = Contacts(fun () -> active.Value.InFixedUpdate) :> IContactDispatcher
    let registry = Registry(Continuations(), contacts)
    active <- Some registry
    let mutable next = 0
    let make action start failInit failBegin =
        next <- next + 1
        let name = "FixedUpdateTest" + string next
        let instance = TickComponent(Action = action, FailInit = failInit, FailBegin = failBegin)
        registry.Register(name, Func<Component>(fun () -> instance))
        let callbacks = ScriptLifecycle()
        callbacks.OnInitialized <- Action<Component>(fun script -> (script :?> TickComponent).OnInitialized())
        callbacks.OnBeginSimulation <- Action<Component>(fun script -> (script :?> TickComponent).OnBeginSimulation())
        callbacks.OnEndSimulation <- Action<Component>(fun script -> (script :?> TickComponent).OnEndSimulation())
        callbacks.OnUninitializing <- Action<Component>(fun script -> (script :?> TickComponent).OnUninitializing())
        callbacks.OnTickSimulate <- Action<Component, single>(fun script dt -> (script :?> TickComponent).OnTickSimulate(dt))
        registry.RegisterLifecycle(name, callbacks)
        let id = registry.Create(ObjectHandle(7u, 3u), name)
        registry.DispatchLifecycle(id, 0) |> ignore
        registry.DispatchLifecycle(id, 1) |> ignore
        if start then registry.DispatchLifecycle(id, 2) |> ignore
        instance, id
    let normal, _ = make null true false false
    check (normal.Calls = 0) "lifecycle does not invoke update"
    registry.UpdateTick(1.0f / 60.0f)
    check (normal.Calls = 1 && normal.Dt = 1.0f / 60.0f && normal.Thread = Environment.CurrentManagedThreadId) "fixed dt on owner thread"
    let initFailed, _ = make null true true false
    let beginFailed, _ = make null true false true
    registry.UpdateTick(1.0f / 60.0f)
    check (initFailed.Calls = 0 && beginFailed.Calls = 0) "failed init/begin never update"
    let faultBaseline = ProbeErrorCount()
    let normalBaseline = normal.Calls
    let notStarted, _ = make null false false false
    normal.Enabled <- false
    registry.UpdateTick(1.0f / 60.0f)
    check (normal.Calls = normalBaseline && notStarted.Calls = 0) "disabled and unstarted do not update"
    normal.Enabled <- true
    let mutable spawned: TickComponent = null
    make (Action<_, _>(fun _ _ ->
        if isNull spawned then spawned <- fst (make null true false false)
        registry.FlushRegistrations())) true false false |> ignore
    registry.UpdateTick(1.0f / 60.0f)
    check (not (isNull spawned) && spawned.Calls = 0) "reentrant registration joins next batch"
    registry.UpdateTick(1.0f / 60.0f)
    check (spawned.Calls = 1) "spawned script updates on next tick"
    let bad, _ = make (Action<_, _>(fun _ _ -> invalidOp "injected")) true false false
    let before = normal.Calls
    registry.UpdateTick(1.0f / 60.0f)
    registry.UpdateTick(1.0f / 60.0f)
    check (bad.Calls = 1 && not bad.Enabled && normal.Calls = before + 2 && ProbeErrorCount() = faultBaseline + 1) "exception isolates one script"
    let destroyed, _ = make (Action<_, _>(fun self _ -> registry.Destroy(self.InstanceId) |> ignore)) true false false
    registry.UpdateTick(1.0f / 60.0f)
    registry.UpdateTick(1.0f / 60.0f)
    check (destroyed.Calls = 1 && destroyed.Ends = 1) "self removal tears down once"
    let ddol, ddolId = make null true false false
    registry.DispatchLifecycle(ddolId, 4) |> ignore
    registry.DispatchLifecycle(ddolId, 1) |> ignore
    registry.UpdateTick(1.0f / 60.0f)
    check (ddol.Begins = 1 && ddol.Calls = 1 && ddol.Ends = 0) "DDOL retains update and lifetime"
    let mutable cleaned = 0
    ddol.Lifetime.RegisterCleanup(Action(fun () -> cleaned <- cleaned + 1; invalidOp "cleanup injection"))
    ddol.Lifetime.RegisterCleanup(Action(fun () -> cleaned <- cleaned + 1))
    registry.DispatchLifecycle(ddolId, 3) |> ignore
    check (cleaned = 2 && ddol.Ends = 1) "cleanup fault does not skip sibling/end hook"
    registry.UpdateTick(1.0f / 60.0f)
    check (ddol.Calls = 1) "ended instance never resumes"
    let orphan, _ = make null true false false
    ProbeSetOwnerAlive(0)
    registry.SweepOrphans() |> ignore
    registry.FlushRegistrations()
    ProbeSetOwnerAlive(1)
    check (orphan.Ends = 1 && orphan.Uninitialized = 1) "orphan cleanup survives scope removal"
    let rejected = TickComponent()
    registry.Register("RejectedDuringReload", Func<Component>(fun () -> rejected))
    let cleanupReentry, _ = make null true false false
    cleanupReentry.End <- Action(fun () ->
        try registry.Create(ObjectHandle(7u, 3u), "RejectedDuringReload") |> ignore
        with :? InvalidOperationException -> ())
    let reloader, _ = make (Action<_, _>(fun _ _ -> registry.Clear())) true false false
    let afterReload, _ = make null true false false
    registry.UpdateTick(1.0f / 60.0f)
    check (afterReload.Calls = 0 && afterReload.Ends = 1 && registry.ActiveCount = 0) "reload invalidates current batch"
    check (reloader.Uninitialized = 1) "reload cleanup once"
    check (rejected.InstanceId > 0 && isNull (registry.Find(rejected.InstanceId)) && not rejected.IsAlive) "rejected reentry does not retain unbound instance"
    check (typeof<Registry>.Assembly = typeof<Component>.Assembly) "F# runtime and native binding share resident assembly"
    registry.ReleaseLookup()
    Console.WriteLine($"FIXED_UPDATE_MANAGED_OK checks={checks} backend=fsharp-native")

let runBodyIdentity() =
    PhysicsComponents.Register()
    let mutable checks = 0
    let check value message =
        if not value then invalidOp message
        checks <- checks + 1
    let entity = Entity(ObjectHandle(7u, 3u))
    ProbeReplaceBody(101UL, 0.0f)
    let old = entity.GetComponent<PhysicsBodyComponent>()
    check (old.ComponentId = 101UL) "lookup captures original identity"
    check (old.Remove() = PhysicsError.None) "remove original body"
    check (isNull (entity.GetComponent<PhysicsBodyComponent>())) "owner has no body after removal"
    ProbeReplaceBody(202UL, 7.0f)
    let replacement = entity.GetComponent<PhysicsBodyComponent>()
    check (replacement.ComponentId = 202UL) "same owner lookup captures replacement identity"
    check (old.ComponentId = 101UL) "old wrapper retains identity"
    let mutable stale = Unchecked.defaultof<PhysicsBodyState>
    check (old.ReadState(&stale) = PhysicsError.StaleHandle && stale.Mass = 0.0f) "old read fails and resets output"
    check (old.SetVelocity(Float3(99.0f, 0.0f, 0.0f)) = PhysicsError.StaleHandle && ProbeBodyVelocity() = 7.0f) "old mutation cannot modify replacement"
    check (old.Remove() = PhysicsError.StaleHandle) "old removal cannot remove replacement"
    check (isNull (PhysicsBodyComponent.Find(entity, 101UL))) "explicit old lookup cannot retarget"
    check (PhysicsBodyComponent.Find(entity, 202UL).ComponentId = 202UL) "explicit new lookup succeeds"
    check (replacement.SetVelocity(Float3(11.0f, 0.0f, 0.0f)) = PhysicsError.None && ProbeBodyVelocity() = 11.0f) "replacement remains writable"
    let mutable state = Unchecked.defaultof<PhysicsBodyState>
    check (replacement.ReadState(&state) = PhysicsError.None && state.LinearVelocity.X = 11.0f) "replacement state is independent"
    check (isNull (PhysicsBodyComponent.Find(Entity(ObjectHandle(7u, 4u)), 202UL))) "owner generation isolates replacement"
    check (replacement.SetShapeRole(1u, ShapeRoles.Attack) = PhysicsError.None && Marshal.PtrToStringUTF8(ProbeBodyRole()) = ShapeRoles.Attack.Id.ToString("D")) "role mutation sends canonical UTF-8 UUID"
    check (replacement.SetShapeRole(1u, Unchecked.defaultof<ShapeRole>) = PhysicsError.None && Marshal.PtrToStringUTF8(ProbeBodyRole()) = "") "empty role sends empty UTF-8 string"
    check (replacement.SetShapeRole(9999u, ShapeRoles.Attack) = PhysicsError.StaleHandle) "unknown role shape retains native error"
    ProbeReplaceBody(101UL, 0.0f)
    Console.WriteLine($"PHYSICS_BODY_IDENTITY_OK checks={checks} backend=fsharp-native")

[<AllowNullLiteral>]
type private ContactComponent() as this =
    inherit Component()
    do
        this.BindEntity(Entity(ObjectHandle(7u, 3u)))
        this.MarkInitialized()
        this.MarkInitializeSucceeded()
        this.MarkBeginSucceeded()
        this.SetApplyEnabled(Action<Component, bool>(fun script enabled -> script.SetEnabledState(enabled) |> ignore))

let runContacts() =
    let mutable checks = 0
    let check value message =
        if not value then invalidOp message
        checks <- checks + 1
    let reject action message =
        let rejected = try action(); false with _ -> true
        check rejected message
    let mutable updating = false
    let contacts = Contacts(fun () -> updating) :> IContactDispatcher
    let source, target = ObjectHandle(1u, 1u), ObjectHandle(2u, 1u)
    let observe instance owner phases capacity grouping = contacts.Observe(instance, owner, ShapeRoles.Attack, ShapeRoles.Hurt, phases, capacity, grouping)
    let instance = ContactComponent()
    contacts.BindRole(source, 11UL, 1u, ShapeRoles.Attack, instance.Lifetime)
    contacts.BindRole(target, 22UL, 2u, ShapeRoles.Hurt, instance.Lifetime)
    let stream = observe instance source ContactPhases.All 4 ContactGrouping.ShapePairs
    let reverse = contacts.Observe(instance, target, ShapeRoles.Hurt, ShapeRoles.Attack, ContactPhases.Begin, 4, ContactGrouping.ShapePairs)
    let unrelated = observe instance (ObjectHandle(3u, 1u)) ContactPhases.All 4 ContactGrouping.ShapePairs
    let mutable first = Unchecked.defaultof<ContactEndpoint>
    first.Owner <- source
    first.Component <- 11UL
    first.Shape <- 1u
    first.BodyGeneration <- 3u
    let mutable second = Unchecked.defaultof<ContactEndpoint>
    second.Owner <- target
    second.Component <- 22UL
    second.Shape <- 2u
    second.BodyGeneration <- 9u
    let mutable value = Unchecked.defaultof<NativeContact>
    value.First <- first
    value.Second <- second
    value.Tick <- 100UL
    contacts.Route(value)
    check (Marshal.SizeOf<ContactEndpoint>() = 48 && Marshal.SizeOf<NativeContact>() = 128) "native contact ABI layout"
    check (stream.Read().Length = 1 && reverse.Read().Length = 1 && unrelated.Read().IsEmpty) "indexed oriented routing"
    check (stream.Read().[0].OtherEntity.Handle = target && reverse.Read().[0].OtherEntity.Handle = source) "endpoint orientation"
    check (stream.Read().[0].SelfShapeId = 1u && stream.Read().[0].OtherShapeId = 2u) "shape identity"
    check (stream.Read().[0].SelfBodyGeneration = 3u && stream.Read().[0].OtherBodyGeneration = 9u && reverse.Read().[0].SelfBodyGeneration = 9u) "oriented body generations"
    value.Tick <- 101UL
    value.Kind <- 1
    contacts.Route(value)
    value.Tick <- 102UL
    value.Kind <- 2
    contacts.Route(value)
    check (stream.Read().Length = 3 && stream.Read().[2].Tick = 102UL && stream.Read().[1].Phase = ContactPhases.Persist) "catchup ticks retained"
    check (reverse.Read().Length = 1) "phase filter"
    contacts.EndFrame()
    check (stream.Read().IsEmpty && stream.RequiredCapacity = 0) "no-step frame clears presentation"
    value.Kind <- 3
    contacts.Route(value)
    check (stream.Read().[0].Sensor && stream.Read().[0].Phase = ContactPhases.Begin) "sensor begin"
    contacts.EndFrame()
    value.First.Owner <- ObjectHandle(1u, 2u)
    contacts.Route(value)
    check (stream.Read().IsEmpty) "entity generation isolation"
    value.First.Owner <- source
    value.First.Component <- 99UL
    contacts.Route(value)
    check (stream.Read().IsEmpty) "instance replacement isolation"
    value.First.Component <- 11UL
    value.First.Shape <- 99u
    contacts.Route(value)
    check (stream.Read().IsEmpty) "shape isolation"
    value.First.Shape <- 1u
    instance.Enabled <- false
    contacts.Route(value)
    check (stream.Read().IsEmpty) "disabled subscriber"
    instance.Enabled <- true
    value.Kind <- 0
    for _ in 1 .. 5 do contacts.Route(value)
    check (stream.Overflowed && stream.RequiredCapacity = 5) "bounded overflow"
    reject (fun () -> stream.Read().Length |> ignore) "partial results rejected"
    contacts.EndFrame()
    for _ in 1 .. 2000 do
        contacts.Route(value)
        contacts.EndFrame()
    let before = GC.GetAllocatedBytesForCurrentThread()
    for _ in 1 .. 10000 do
        contacts.Route(value)
        if stream.Read().Length <> 1 then invalidOp "lost contact"
        contacts.EndFrame()
    let allocated = GC.GetAllocatedBytesForCurrentThread() - before
    check (allocated = 0L) "steady routing/read/clear allocation zero"
    instance.Lifetime.End()
    reject (fun () -> stream.Read().Length |> ignore) "lifetime disposes stream"
    contacts.Route(value)
    contacts.EndFrame()
    reject (fun () -> contacts.Observe(instance, source, Unchecked.defaultof<ShapeRole>, ShapeRoles.Hurt, ContactPhases.All, 4, ContactGrouping.ShapePairs) |> ignore) "invalid role"
    check (Marshal.OffsetOf<NativeContact>("Tick").ToInt32() = 96) "native tick offset"
    let authored = ContactComponent()
    let authoredStream = observe authored source ContactPhases.All 4 ContactGrouping.ShapePairs
    value.First.Role <- ShapeRoles.Attack.Id
    value.Second.Role <- ShapeRoles.Hurt.Id
    contacts.Route(value)
    check (authoredStream.Read().Length = 1) "stored roles need no explicit binding"
    contacts.EndFrame()
    let authoredBefore = GC.GetAllocatedBytesForCurrentThread()
    for _ in 1 .. 10000 do
        contacts.Route(value)
        if authoredStream.Read().Length <> 1 then invalidOp "lost authored contact"
        contacts.EndFrame()
    check (GC.GetAllocatedBytesForCurrentThread() = authoredBefore) "stored role steady allocation zero"
    value.First.Role <- ShapeRoles.Hurt.Id
    contacts.Route(value)
    check (authoredStream.Read().IsEmpty) "changed role isolates previous subscription"
    contacts.EndFrame()
    authored.Lifetime.End()
    reject (fun () -> authoredStream.Read().Length |> ignore) "stored role cleanup"
    let lateComponent = ContactComponent()
    let late = observe lateComponent source ContactPhases.All 4 ContactGrouping.ShapePairs
    let beginOnly = observe lateComponent source ContactPhases.Begin 4 ContactGrouping.ShapePairs
    value.First.Role <- ShapeRoles.Attack.Id
    value.Kind <- 5
    contacts.Route(value)
    check (late.Read().Length = 1 && late.Read().[0].Sensor && late.Read().[0].Phase = ContactPhases.Begin) "initial Persist seeds sensor Begin"
    check (beginOnly.Read().Length = 1) "initial Begin-only overlap"
    contacts.EndFrame()
    contacts.Route(value)
    check (late.Read().Length = 1 && late.Read().[0].Phase = ContactPhases.Persist && beginOnly.Read().IsEmpty) "Persist does not repeat initial Begin"
    contacts.EndFrame()
    let sensorBefore = GC.GetAllocatedBytesForCurrentThread()
    for _ in 1 .. 10000 do
        contacts.Route(value)
        contacts.EndFrame()
    let sensorAllocated = GC.GetAllocatedBytesForCurrentThread() - sensorBefore
    check (sensorAllocated = 0L) $"sensor steady allocation zero: {sensorAllocated}"
    value.Kind <- 4
    contacts.Route(value)
    check (late.Read().[0].Phase = ContactPhases.End && beginOnly.Read().IsEmpty) "sensor End filter"
    contacts.EndFrame()
    value.Kind <- 5
    contacts.Route(value)
    check (beginOnly.Read().Length = 1) "End removes overlap state for reentry"
    contacts.EndFrame()
    value.First.BodyGeneration <- value.First.BodyGeneration + 1u
    contacts.Route(value)
    check (beginOnly.Read().Length = 1) "sensor body generation isolation"
    contacts.EndFrame()
    value.First.BodySlot <- value.First.BodySlot + 1u
    contacts.Route(value)
    check (beginOnly.Read().Length = 1) "sensor body slot isolation"
    contacts.EndFrame()
    lateComponent.Lifetime.End()
    let groupedComponent = ContactComponent()
    let grouped = observe groupedComponent source ContactPhases.All 4 ContactGrouping.SensorTargets
    value.Kind <- 3
    value.Tick <- 200UL
    value.Second.Shape <- 2u
    contacts.Route(value)
    value.Second.Shape <- 3u
    contacts.Route(value)
    check (grouped.Read().Length = 1 && grouped.Read().[0].Phase = ContactPhases.Begin) "two shapes emit one target Begin"
    contacts.EndFrame()
    value.Kind <- 5
    value.Tick <- 201UL
    contacts.Route(value)
    value.Second.Shape <- 2u
    contacts.Route(value)
    check (grouped.Read().Length = 1 && grouped.Read().[0].Phase = ContactPhases.Persist) "target Persist once per tick"
    value.Tick <- 202UL
    contacts.Route(value)
    check (grouped.Read().Length = 2 && grouped.Read().[1].Tick = 202UL) "target catchup ticks preserved"
    contacts.EndFrame()
    value.Kind <- 4
    contacts.Route(value)
    check (grouped.Read().IsEmpty) "partial target exit has no End"
    value.Second.Shape <- 3u
    contacts.Route(value)
    check (grouped.Read().Length = 1 && grouped.Read().[0].Phase = ContactPhases.End) "last target exit emits End"
    contacts.EndFrame()
    contacts.Route(value)
    check (grouped.Read().IsEmpty) "unknown target End ignored"
    value.Kind <- 5
    value.Tick <- 203UL
    contacts.Route(value)
    check (grouped.Read().[0].Phase = ContactPhases.Begin) "late target seeds Begin"
    contacts.EndFrame()
    value.Second.Owner <- ObjectHandle(2u, 2u)
    value.Tick <- 204UL
    contacts.Route(value)
    check (grouped.Read().[0].OtherEntity.Handle = value.Second.Owner && grouped.Read().[0].Phase = ContactPhases.Begin) "target generation isolation"
    contacts.EndFrame()
    for _ in 1 .. 2000 do
        value.Tick <- value.Tick + 1UL
        contacts.Route(value)
        contacts.EndFrame()
    let groupedBefore = GC.GetAllocatedBytesForCurrentThread()
    for _ in 1 .. 10000 do
        value.Tick <- value.Tick + 1UL
        contacts.Route(value)
        if grouped.Read().Length <> 1 then invalidOp "lost grouped contact"
        contacts.EndFrame()
    check (GC.GetAllocatedBytesForCurrentThread() = groupedBefore) "grouped steady allocation zero"
    value.Kind <- 4
    contacts.Route(value)
    contacts.EndFrame()
    value.Kind <- 3
    value.Second.Owner <- target
    value.Tick <- value.Tick + 1UL
    for shape in 10u .. 13u do
        value.Second.Shape <- shape
        contacts.Route(value)
    check grouped.Overflowed "active pair capacity bounded"
    reject (fun () -> grouped.Read().Length |> ignore) "grouped partial results rejected"
    contacts.EndFrame()
    reject (fun () -> observe groupedComponent source ContactPhases.All 4 (enum<ContactGrouping> 99) |> ignore) "invalid grouping rejected"
    groupedComponent.Lifetime.End()
    reject (fun () -> grouped.Read().Length |> ignore) "grouped cleanup"
    let suspendedComponent = ContactComponent()
    let suspended = observe suspendedComponent source ContactPhases.All 4 ContactGrouping.ShapePairs
    let suspendedTargets = observe suspendedComponent source ContactPhases.All 4 ContactGrouping.SensorTargets
    let suspendedBegin = observe suspendedComponent source ContactPhases.Begin 4 ContactGrouping.ShapePairs
    let independentComponent = ContactComponent()
    let independent = observe independentComponent source ContactPhases.All 4 ContactGrouping.ShapePairs
    value.Kind <- 3
    value.Second.Shape <- 2u
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    suspendedComponent.Enabled <- false
    contacts.Suspend(suspendedComponent)
    check (suspended.Read().IsEmpty && suspendedTargets.Read().IsEmpty && suspendedBegin.Read().IsEmpty) "disable clears pending events"
    check (independent.Read().Length = 1) "disable isolates subscriber sharing route"
    value.Kind <- 4
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    check (suspended.Read().IsEmpty) "disabled End not queued"
    contacts.EndFrame()
    value.Kind <- 3
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    check (suspended.Read().IsEmpty) "disabled Begin not queued"
    contacts.EndFrame()
    suspendedComponent.Enabled <- true
    check (suspended.Read().IsEmpty) "enable does not replay disabled history"
    value.Kind <- 5
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    check (suspended.Read().Length = 1 && suspended.Read().[0].Phase = ContactPhases.Begin
           && suspendedTargets.Read().Length = 1 && suspendedTargets.Read().[0].Phase = ContactPhases.Begin && suspendedBegin.Read().Length = 1) "enable seeds current overlaps"
    contacts.EndFrame()
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    check (suspended.Read().[0].Phase = ContactPhases.Persist && suspendedTargets.Read().[0].Phase = ContactPhases.Persist && suspendedBegin.Read().IsEmpty) "reactivated Begin once"
    suspendedComponent.Enabled <- false
    contacts.Suspend(suspendedComponent)
    suspendedComponent.Enabled <- true
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    check (suspended.Read().Length = 1 && suspended.Read().[0].Phase = ContactPhases.Begin) "same-frame disable/enable resets observation"
    contacts.EndFrame()
    check (suspended.Read().IsEmpty) "same-frame reset leaves no replay"
    suspended.Dispose()
    suspendedComponent.Enabled <- false
    contacts.Suspend(suspendedComponent)
    suspendedComponent.Enabled <- true
    suspendedComponent.Lifetime.End()
    reject (fun () -> suspendedTargets.Read().Length |> ignore) "disable preserves lifetime until cancellation"
    independentComponent.Lifetime.End()
    contacts.EndFrame()
    let transferred = ContactComponent()
    contacts.BindRole(source, 11UL, 1u, ShapeRoles.Attack, transferred.Lifetime)
    let acrossScene = observe transferred source ContactPhases.All 1 ContactGrouping.ShapePairs
    let acrossTargets = observe transferred source ContactPhases.All 1 ContactGrouping.SensorTargets
    value.First.Owner <- source
    value.First.Component <- 11UL
    value.First.Shape <- 1u
    value.First.Role <- Guid.Empty
    value.Second.Role <- ShapeRoles.Hurt.Id
    value.Second.Owner <- target
    value.Kind <- 5
    contacts.Route(value)
    check (acrossScene.Read().Length = 1 && acrossTargets.Read().Length = 1) "source scene overlap"
    contacts.Suspend(transferred)
    check (acrossScene.Read().IsEmpty && acrossTargets.Read().IsEmpty && not acrossScene.Overflowed) "scene detach clears buffers"
    check (transferred.Enabled && not acrossScene.Disposed) "DDOL preserves lifetime"
    value.Second.Owner <- ObjectHandle(2u, 2u)
    contacts.Route(value)
    check (acrossScene.Read().Length = 1 && acrossScene.Read().[0].Phase = ContactPhases.Begin && acrossTargets.Read().Length = 1 && acrossTargets.Read().[0].Phase = ContactPhases.Begin) "destination seeds clean overlap"
    contacts.EndFrame()
    for epoch in 3u .. 99u do
        contacts.Suspend(transferred)
        value.Second.Owner <- ObjectHandle(2u, epoch)
        contacts.Route(value)
        if acrossScene.Read().Length <> 1 || acrossScene.Read().[0].Phase <> ContactPhases.Begin then invalidOp "scene-boundary capacity accumulated"
        contacts.EndFrame()
    check (not acrossScene.Overflowed && not acrossTargets.Overflowed) "repeated scene boundaries bounded"
    transferred.Lifetime.End()
    reject (fun () -> acrossScene.Read().Length |> ignore) "DDOL stream final destruction"
    reject (fun () -> acrossTargets.Read().Length |> ignore) "DDOL target stream final destruction"
    contacts.EndFrame()
    let solidComponent = ContactComponent()
    let solidPairs = observe solidComponent source ContactPhases.All 8 ContactGrouping.ShapePairs
    let mixedTargets = observe solidComponent source ContactPhases.All 8 ContactGrouping.SensorTargets
    let solidReverse = contacts.Observe(solidComponent, target, ShapeRoles.Hurt, ShapeRoles.Attack, ContactPhases.All, 8, ContactGrouping.ShapePairs)
    value.First.Owner <- source
    value.First.Role <- ShapeRoles.Attack.Id
    value.Second.Owner <- target
    value.Second.Role <- ShapeRoles.Hurt.Id
    value.Second.Shape <- 23u
    value.Kind <- 0
    value.ContactCount <- 2
    value.RequiredContacts <- 5
    value.Point <- Float3(1.0f, 2.0f, 3.0f)
    value.Tick <- value.Tick + 1UL
    for shape in [19u; 20u] do
        value.First.Shape <- shape
        contacts.Route(value)
    check (solidPairs.Read().Length = 2 && mixedTargets.Read().Length = 2) "solid Begin preserves shape pairs"
    check (not (mixedTargets.Read().[0].Sensor) && mixedTargets.Read().[0].SelfShapeId = 19u && mixedTargets.Read().[1].SelfShapeId = 20u) "solid shape identity preserved"
    check (solidReverse.Read().Length = 2 && solidReverse.Read().[0].OtherShapeId = 19u && solidReverse.Read().[0].SelfShapeId = 23u) "solid reverse orientation"
    check (solidReverse.Read().[0].ContactCount = 2 && solidReverse.Read().[0].RequiredContacts = 5 && solidReverse.Read().[0].Point.X = 1.0f && solidReverse.Read().[0].Point.Y = 2.0f) "solid reverse payload/truncation"
    contacts.EndFrame()
    value.Kind <- 1
    value.Tick <- value.Tick + 1UL
    for shape in [19u; 20u] do
        value.First.Shape <- shape
        contacts.Route(value)
    check (mixedTargets.Read().Length = 2 && mixedTargets.Read().[0].Phase = ContactPhases.Persist && mixedTargets.Read().[1].Tick = value.Tick) "solid Persist not deduplicated"
    contacts.EndFrame()
    value.First.Shape <- 19u
    value.Kind <- 2
    contacts.Route(value)
    value.First.Shape <- 20u
    value.Kind <- 1
    contacts.Route(value)
    check (mixedTargets.Read().Length = 2 && mixedTargets.Read().[0].Phase = ContactPhases.End && mixedTargets.Read().[1].Phase = ContactPhases.Persist) "partial solid separation"
    contacts.EndFrame()
    value.Kind <- 2
    contacts.Route(value)
    check (mixedTargets.Read().Length = 1 && mixedTargets.Read().[0].SelfShapeId = 20u) "last solid separation identity"
    contacts.EndFrame()
    check (mixedTargets.Read().IsEmpty) "solid no-step no replay"
    solidComponent.Lifetime.End()
    reject (fun () -> mixedTargets.Read().Length |> ignore) "solid stream lifetime disposal"
    let tickOwner = ContactComponent()
    let tickStream = observe tickOwner source ContactPhases.All 4 ContactGrouping.ShapePairs
    value.First.Role <- ShapeRoles.Attack.Id
    value.Second.Role <- ShapeRoles.Hurt.Id
    value.First.Owner <- source
    value.Second.Owner <- target
    value.Kind <- 0
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    contacts.EndFrame()
    check (tickStream.Read().IsEmpty) "presentation clears zero-step frame"
    updating <- true
    check (tickStream.Read().Length = 1) "next fixed update retains prior tick"
    contacts.EndUpdate()
    check (tickStream.Read().IsEmpty) "prior tick consumed once"
    updating <- false
    value.Kind <- 1
    value.Tick <- value.Tick + 1UL
    contacts.Route(value)
    updating <- true
    check (tickStream.Read().Length = 1 && tickStream.Read().[0].Tick = value.Tick) "catchup reads preceding tick"
    contacts.EndUpdate()
    updating <- false
    check (tickStream.Read().Length = 1) "fixed consumption preserves presentation"
    tickOwner.Lifetime.End()
    reject (fun () -> tickStream.Read().Length |> ignore) "lifetime retires both buffers"
    Console.WriteLine($"CONTACT_STREAM_OK checks={checks} steadyAllocatedBytes={allocated} backend=fsharp-native")
