namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsSensorTransitionProbe() =
    inherit Component()
    let mutable pairs = Unchecked.defaultof<ContactStream>
    let mutable targets = Unchecked.defaultof<ContactStream>
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let identities = Array.zeroCreate<uint64> 3
    let sensorBegins = Array.zeroCreate<int> 3
    let sensorPersists = Array.zeroCreate<int> 3
    let sensorEnds = Array.zeroCreate<int> 3
    let mutable solidBegin = 0
    let mutable solidPersist = 0
    let mutable solidEnd = 0
    let mutable targetBegin = 0
    let mutable targetPersist = 0
    let mutable targetEnd = 0
    let mutable targetSolidBegin = 0
    let mutable targetSolidEnd = 0
    let mutable stage = 0
    let mutable tick = 0UL
    let mutable persistTick = 0UL
    let mutable other = Unchecked.defaultof<Entity>
    let mutable otherComponent = 0UL
    let mutable elapsed = 0.0f
    let mutable settledAt = 0.0f
    let mutable finished = false

    member private _.Require(error: PhysicsError) =
        if error <> PhysicsError.None then invalidOp $"Transition operation: {error}"

    member private _.CheckFlags(sensor: bool) =
        let mutable shape = Unchecked.defaultof<PhysicsShapeState>
        if body.GetShape(1, &shape) <> PhysicsError.None || shape.ShapeId <> 19u
           || shape.Sensor <> sensor || not shape.QueryEnabled || shape.ContactRole <> ShapeRoles.Attack then
            invalidOp "Transition shape flags/role not applied or restored."

    member private this.Replace(sensor: bool) =
        this.Require(body.SetShapeFlags(19u, sensor, true))
        this.CheckFlags(sensor)

    member this.OnBeginSimulation() =
        body <- this.GetComponent<PhysicsBodyComponent>()
        if obj.ReferenceEquals(body, null) then invalidOp "Missing transition body."
        this.CheckFlags(true)
        pairs <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 64)
        targets <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 64, ContactGrouping.SensorTargets)
        PhysicsContactStreamProbe.Track(pairs)
        PhysicsContactStreamProbe.Track(targets)

    member private this.Print(success, error: string) =
        Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; success = success; error = error; sensorTransition = true;
            sensorBegins = sensorBegins; sensorPersists = sensorPersists; sensorEnds = sensorEnds;
            solidBegin = solidBegin; solidPersist = solidPersist; solidEnd = solidEnd;
            groupBegin = targetBegin; groupPersist = targetPersist; groupEnd = targetEnd;
            groupSolidBegin = targetSolidBegin; groupSolidEnd = targetSolidEnd; bodies = identities; tick = tick |}).Substring(1))

    member this.PostPhysics(dt: single) =
        if not finished then
            try
                elapsed <- elapsed + dt
                let values = pairs.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if contact.SelfEntity <> this.Entity || contact.SelfComponentId <> body.ComponentId
                       || contact.OtherShapeId <> 23u || (contact.SelfShapeId <> 19u && contact.SelfShapeId <> 20u)
                       || contact.Tick = 0UL || contact.Tick < tick then invalidOp "Transition endpoint/tick mismatch."
                    tick <- contact.Tick
                    if not other.IsAlive then
                        other <- contact.OtherEntity
                        otherComponent <- contact.OtherComponentId
                    if contact.OtherEntity <> other || contact.OtherComponentId <> otherComponent then invalidOp "Transition target changed."
                    let identity = (uint64 contact.SelfBodySlot <<< 32) ||| uint64 contact.SelfBodyGeneration
                    let mutable index = Array.IndexOf(identities, identity)
                    if index < 0 then
                        if stage > 2 || identities[stage] <> 0UL then invalidOp "Unexpected transition body."
                        index <- stage
                        identities[index] <- identity
                    if contact.Phase <> ContactPhases.End && index <> stage then invalidOp "Retired transition body emitted stale contact."
                    if contact.Sensor <> (index <> 1 || contact.SelfShapeId = 20u) then invalidOp "Sensor/solid snapshot flag mismatch."
                    if contact.Sensor then
                        if contact.Phase = ContactPhases.Begin then sensorBegins[index] <- sensorBegins[index] + 1
                        elif contact.Phase = ContactPhases.Persist then sensorPersists[index] <- sensorPersists[index] + 1
                        else sensorEnds[index] <- sensorEnds[index] + 1
                    else
                        if contact.Phase = ContactPhases.Begin then solidBegin <- solidBegin + 1
                        elif contact.Phase = ContactPhases.Persist then solidPersist <- solidPersist + 1
                        else solidEnd <- solidEnd + 1
                let values = targets.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if contact.OtherEntity <> other then invalidOp "Grouped target changed."
                    if not contact.Sensor then
                        if contact.Phase = ContactPhases.Begin then targetSolidBegin <- targetSolidBegin + 1
                        elif contact.Phase = ContactPhases.End then targetSolidEnd <- targetSolidEnd + 1
                    elif contact.Phase = ContactPhases.Begin then targetBegin <- targetBegin + 1
                    elif contact.Phase = ContactPhases.End then targetEnd <- targetEnd + 1
                    else
                        if contact.Tick <= persistTick then invalidOp "Duplicate target Persist."
                        persistTick <- contact.Tick
                        targetPersist <- targetPersist + 1
                if stage = 0 && sensorBegins[0] = 2 && sensorPersists[0] >= 2 then
                    this.Replace(false)
                    stage <- 1
                elif stage = 1 && sensorEnds[0] = 2 && sensorBegins[1] = 1 && sensorPersists[1] > 0 && solidBegin = 1 && solidPersist > 0 then
                    this.Replace(true)
                    stage <- 2
                elif stage = 2 && sensorEnds[1] = 1 && solidEnd = 1 && sensorBegins[2] = 2 && sensorPersists[2] >= 2 then
                    this.Transform.WorldPosition <- Float3(10.0f, 0.0f, 0.0f)
                    this.Require(body.SetVelocity(Unchecked.defaultof<Float3>))
                    stage <- 3
                elif stage = 3 && sensorEnds[2] = 2 then
                    stage <- 4
                    settledAt <- elapsed
                elif stage = 4 && elapsed - settledAt > 0.1f then
                    if sensorBegins <> [|2; 1; 2|] || sensorEnds <> [|2; 1; 2|] || solidBegin <> 1 || solidEnd <> 1
                       || targetBegin < 1 || targetBegin > 3 || targetEnd <> targetBegin || targetSolidBegin <> 1 || targetSolidEnd <> 1
                       || identities[0] = identities[1] || identities[1] = identities[2] || identities[0] = identities[2] then
                        invalidOp "Transition counts or identity acceptance failed."
                    finished <- true
                    this.Print(true, "")
                if elapsed > 15.0f then invalidOp "Sensor transition timeout."
            with error ->
                finished <- true
                this.Print(false, error.Message)
                reraise()
