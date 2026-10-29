namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsTopologyContactProbe() =
    inherit Component()
    let mutable replacementCalls = 0
    let mutable pairs = Unchecked.defaultof<ContactStream>
    let mutable targets = Unchecked.defaultof<ContactStream>
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable other = Unchecked.defaultof<Entity>
    let mutable otherComponent = 0UL
    let mutable tick = 0UL
    let mutable targetTick = 0UL
    let mutable oldBody = 0UL
    let mutable newBody = 0UL
    let mutable begins = 0
    let mutable persists = 0
    let mutable ends = 0
    let mutable targetBegins = 0
    let mutable targetPersists = 0
    let mutable targetEnds = 0
    let mutable stage = 0
    let mutable oldEnds = 0
    let mutable newBegins = 0
    let mutable newEnds = 0
    let mutable elapsed = 0.0f
    let mutable finishedAt = 0.0f
    let mutable finished = false
    [<DefaultValue; SerializeField>] val mutable public _burstReplacements: bool

    member private _.Require(error: PhysicsError) =
        if error <> PhysicsError.None then invalidOp $"Topology operation: {error}"

    member private this.Replace(shape: uint32, sensor: bool, query: bool) =
        this.Require(body.SetShapeFlags(shape, sensor, query))
        replacementCalls <- replacementCalls + 1

    member this.OnBeginSimulation() =
        body <- this.GetComponent<PhysicsBodyComponent>()
        if obj.ReferenceEquals(body, null) then invalidOp "Missing topology body."
        let mutable original = Unchecked.defaultof<PhysicsShapeState>
        if body.GetShape(1, &original) <> PhysicsError.None || original.ShapeId <> 19u || not original.QueryEnabled
           || not original.Sensor || original.ContactRole <> ShapeRoles.Attack then
            invalidOp "Authored shape flags were not restored before Play."
        pairs <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 64)
        targets <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 64, ContactGrouping.SensorTargets)
        PhysicsContactStreamProbe.Track(pairs)
        PhysicsContactStreamProbe.Track(targets)

    member private this.Print(success, error: string) =
        Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; success = success; error = error;
            topology = true; burst = this._burstReplacements; replacementCalls = replacementCalls;
            sensorBegin = begins; sensorPersist = persists; sensorEnd = ends;
            groupBegin = targetBegins; groupPersist = targetPersists; groupEnd = targetEnds;
            oldEnds = oldEnds; newBegins = newBegins; newEnds = newEnds; oldBodyIdentity = oldBody; newBodyIdentity = newBody;
            oldSlot = oldBody >>> 32; newSlot = newBody >>> 32; oldSlotGeneration = uint32 oldBody; newSlotGeneration = uint32 newBody; tick = tick |}).Substring(1))

    member this.PostPhysics(dt: single) =
        if not finished then
            try
                elapsed <- elapsed + dt
                let values = pairs.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if not contact.Sensor || contact.SelfEntity <> this.Entity || contact.OtherShapeId <> 23u
                       || (contact.SelfShapeId <> 19u && contact.SelfShapeId <> 20u) || contact.SelfComponentId <> body.ComponentId
                       || contact.Tick = 0UL || contact.Tick < tick then invalidOp "Topology endpoint/tick mismatch."
                    tick <- contact.Tick
                    let identity = (uint64 contact.SelfBodySlot <<< 32) ||| uint64 contact.SelfBodyGeneration
                    if oldBody = 0UL then
                        oldBody <- identity
                        other <- contact.OtherEntity
                        otherComponent <- contact.OtherComponentId
                    if contact.OtherEntity <> other || contact.OtherComponentId <> otherComponent then invalidOp "Topology target identity changed."
                    if stage = 0 && identity <> oldBody then invalidOp "Body changed before replacement."
                    if stage > 0 && identity = oldBody && contact.Phase <> ContactPhases.End then invalidOp "Retired generation produced stale contact."
                    if identity <> oldBody then
                        if newBody = 0UL then newBody <- identity
                        if identity <> newBody then invalidOp "Unexpected third body generation."
                    if contact.Phase = ContactPhases.End && identity = oldBody then oldEnds <- oldEnds + 1
                    if contact.Phase = ContactPhases.Begin && identity <> oldBody then newBegins <- newBegins + 1
                    if contact.Phase = ContactPhases.End && identity <> oldBody then newEnds <- newEnds + 1
                    if oldEnds > 2 || newBegins > 2 || newEnds > 2 then invalidOp "Topology generation transition repeated."
                    if contact.Phase = ContactPhases.Begin then begins <- begins + 1
                    elif contact.Phase = ContactPhases.Persist then persists <- persists + 1
                    else ends <- ends + 1
                let values = targets.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if not contact.Sensor || contact.OtherEntity <> other then invalidOp "Topology target aggregation mismatch."
                    if contact.Phase = ContactPhases.Begin then targetBegins <- targetBegins + 1
                    elif contact.Phase = ContactPhases.End then targetEnds <- targetEnds + 1
                    else
                        if contact.Tick <= targetTick then invalidOp "Repeated target Persist."
                        targetPersists <- targetPersists + 1
                    targetTick <- contact.Tick
                if stage = 0 && begins = 2 && persists >= 2 then
                    if this._burstReplacements then
                        this.Replace(19u, false, true)
                        this.Replace(19u, true, false)
                        this.Replace(20u, false, true)
                        this.Replace(20u, true, true)
                    else this.Replace(19u, true, false)
                    let mutable shape = Unchecked.defaultof<PhysicsShapeState>
                    if body.GetShape(1, &shape) <> PhysicsError.None || shape.QueryEnabled || not shape.Sensor || shape.ContactRole <> ShapeRoles.Attack then
                        invalidOp "Replacement did not preserve role/sensor or apply query flag."
                    stage <- 1
                elif stage = 1 && begins = 4 && ends = 2 && newBody <> 0UL && persists >= 4 then
                    this.Transform.WorldPosition <- Float3(10.0f, 0.0f, 0.0f)
                    this.Require(body.SetVelocity(Unchecked.defaultof<Float3>))
                    stage <- 2
                elif stage = 2 && ends = 4 then
                    stage <- 3
                    finishedAt <- elapsed
                elif stage = 3 && elapsed - finishedAt > 0.1f then
                    if begins <> 4 || ends <> 4 || targetBegins < 1 || targetBegins > 2 || targetEnds <> targetBegins
                       || targetPersists < 1 || oldBody = newBody || oldEnds <> 2 || newBegins <> 2 || newEnds <> 2 then
                        invalidOp "Topology acceptance incomplete or stale active target."
                    finished <- true
                    this.Print(true, "")
                if elapsed > 15.0f then invalidOp "Topology product timeout."
            with error ->
                finished <- true
                this.Print(false, error.Message)
                reraise()
