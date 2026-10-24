namespace CreatorEngine.Scripts

open System
open CreatorEngine

/// Runs in the real native CPU host, using the normal generated lifecycle table.
type HeadlessTickProbe() =
    inherit Component()

    static let mutable ticks = 0
    static let mutable initialized = 0
    static let mutable began = 0
    static let mutable ended = 0
    static let mutable uninitialized = 0
    static let mutable failures = 0
    static let mutable stale = 0
    static let mutable snapshots = 0
    static let mutable cleanup = 0

    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable retiring = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable target = Unchecked.defaultof<Transform>
    let mutable retirementOwner = Unchecked.defaultof<Entity>
    let payload = Array.zeroCreate<byte> 64
    let mutable resource = Unchecked.defaultof<IDisposable>

    [<DefaultValue; SerializeField>]
    val mutable Speed: single

    [<DefaultValue; SerializeField>]
    val mutable ExpectedCommandInitialX: single

    [<DefaultValue; SerializeField>]
    val mutable FailAtTick: int

    [<DefaultValue; SerializeField>]
    val mutable FailAtEnd: bool

    member _.OnInitialized() : unit =
        initialized <- initialized + 1
        resource <- { new IDisposable with member _.Dispose() = cleanup <- cleanup + 1 }

    member this.OnBeginSimulation() : unit =
        began <- began + 1
        body <- this.GetComponent<PhysicsBodyComponent>()
        retirementOwner <- Entity.Find("Retiring")
        retiring <- retirementOwner.GetComponent<PhysicsBodyComponent>()
        let commandOwner = Entity.Find("CommandTarget")
        target <- commandOwner.GetComponent<Transform>()

        if target.WorldPosition.X <> this.ExpectedCommandInitialX then
            failures <- failures + 1
            invalidOp "Authored initial Transform was not restored"

        if obj.ReferenceEquals(body, null) || this.Speed <> 3.0f then
            failures <- failures + 1
            invalidOp "Native body or cooked F# field was not bound"

        let mutable status = Unchecked.defaultof<SimulationStatus>
        let mutable sequence = 0UL

        if Simulation.GetStatus(&status) <> SimulationResult.Ok
           || Simulation.SetWorldPosition(status.Epoch, 1UL, commandOwner, Float3(10.0f, 0.0f, 0.0f), &sequence) <> SimulationResult.Ok
           || sequence = 0UL then
            failures <- failures + 1
            invalidOp "Copied command submission failed"

    member this.OnTickSimulate(dt: single) : unit =
        let mutable status = Unchecked.defaultof<SimulationStatus>
        let mutable state = Unchecked.defaultof<PhysicsBodyState>

        if Component.FrameCount = 0UL
           || Simulation.GetStatus(&status) <> SimulationResult.Ok
           || status.Executing <> uint64 (ticks + 1)
           || status.Committed <> uint64 ticks
           || abs (dt - 1.0f / 60.0f) > 1e-7f
           || body.ReadState(&state) <> PhysicsError.None
           || target.WorldPosition.X <> 10.0f
           || abs (state.Position.X - single ticks * 3.0f / 60.0f) > 0.001f then
            failures <- failures + 1
            invalidOp "Fixed dt, tick order, or previous physics pose diverged"

        if body.SetVelocity(Float3(3.0f, 0.0f, 0.0f)) <> PhysicsError.None then
            failures <- failures + 1
            invalidOp "Native velocity write failed"

        ticks <- ticks + 1

        if ticks = 2 then
            let mutable snapshot = Unchecked.defaultof<SimulationSnapshot>
            if Simulation.ReadSnapshot(status.Epoch, 0UL, payload.AsSpan(), &snapshot) <> SimulationResult.Ok
               || snapshot.Tick <> 1UL || snapshot.Schema <> 1u || snapshot.Bytes <> 24u
               || snapshot.Applied <> 1u || BitConverter.ToSingle(payload, 8) <> 10.0f then
                failures <- failures + 1
                invalidOp "Post-physics snapshot or command ordering diverged"
            snapshots <- snapshots + 1

            if retiring.Remove() <> PhysicsError.None || retiring.ReadState(&state) <> PhysicsError.StaleHandle then
                failures <- failures + 1
                invalidOp "Removed body wrapper retargeted"
            stale <- stale + 1
            retirementOwner.Destroy()

        if ticks = 3 then
            if retirementOwner.IsAlive then
                failures <- failures + 1
                invalidOp "Deferred entity retirement was not drained"
            stale <- stale + 1

        if this.FailAtTick = ticks then invalidOp "Injected tick failure"

    member this.OnEndSimulation() : unit =
        ended <- ended + 1
        resource.Dispose()
        if this.FailAtEnd then invalidOp "Injected end failure"

    member _.OnUninitializing() : unit =
        uninitialized <- uninitialized + 1

    [<EngineCallable>]
    static member State() =
        String.concat ""
            [ $"{{\"ticks\":{ticks},\"initialized\":{initialized},\"began\":{began}"
              $",\"ended\":{ended},\"uninitialized\":{uninitialized}"
              $",\"failures\":{failures},\"stale\":{stale}"
              $",\"snapshots\":{snapshots},\"cleanup\":{cleanup}"
              $",\"disabledInitialized\":{DisabledLifecycleProbe.Initialized}"
              $",\"disabledEnableCalls\":{DisabledLifecycleProbe.EnabledCalls}"
              $",\"disabledUpdates\":{DisabledLifecycleProbe.Updates}"
              $",\"disabledRetired\":{DisabledLifecycleProbe.Retired}}}" ]
