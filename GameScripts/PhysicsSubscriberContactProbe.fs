namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsSubscriberContactProbe() =
    inherit Component()
    [<DefaultValue>] val mutable private pairs: ContactStream
    [<DefaultValue>] val mutable private targets: ContactStream
    [<DefaultValue>] val mutable private beginOnly: ContactStream
    [<DefaultValue>] val mutable private observer: PhysicsSubscriberContactProbe
    [<DefaultValue>] val mutable private body: PhysicsBodyComponent
    [<DefaultValue>] val mutable private controller: bool
    [<DefaultValue>] val mutable private finished: bool
    [<DefaultValue>] val mutable private stage: int
    [<DefaultValue>] val mutable private begins: int
    [<DefaultValue>] val mutable private persists: int
    [<DefaultValue>] val mutable private ends: int
    [<DefaultValue>] val mutable private targetBegins: int
    [<DefaultValue>] val mutable private targetPersists: int
    [<DefaultValue>] val mutable private targetEnds: int
    [<DefaultValue>] val mutable private beginOnlyCount: int
    [<DefaultValue>] val mutable private disables: int
    [<DefaultValue>] val mutable private enables: int
    [<DefaultValue>] val mutable private callbacks: int
    [<DefaultValue>] val mutable private disabledCallbacks: int
    [<DefaultValue>] val mutable private enableTargetPersists: int
    [<DefaultValue>] val mutable private elapsed: single
    [<DefaultValue>] val mutable private stageAt: single
    [<DefaultValue>] val mutable private lastTick: uint64
    [<DefaultValue>] val mutable private enableAfterTick: uint64
    [<DefaultValue>] val mutable private seedTick: uint64

    member this.OnBeginSimulation() =
        this.controller <- this.Entity.Name = "ContactAttack"
        let owner = Entity.Find("ContactAttack")
        if not owner.IsAlive then invalidOp "Missing attack owner."
        this.pairs <- Physics.ObserveContacts(owner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 32)
        PhysicsContactStreamProbe.Track(this.pairs)
        if this.controller then
            this.body <- this.GetComponent<PhysicsBodyComponent>()
            if obj.ReferenceEquals(this.body, null) then invalidOp "Missing controller body."
            this.observer <- Entity.Find("ContactObserver").GetComponent<PhysicsSubscriberContactProbe>()
            if obj.ReferenceEquals(this.observer, null) then invalidOp "Missing bodyless observer."
        else
            if this.Entity.HasComponent<PhysicsBodyComponent>() then invalidOp "Observer must have no body."
            this.targets <- Physics.ObserveContacts(owner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 32, ContactGrouping.SensorTargets)
            this.beginOnly <- Physics.ObserveContacts(owner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.Begin, 32)
            PhysicsContactStreamProbe.Track(this.targets)
            PhysicsContactStreamProbe.Track(this.beginOnly)

    member private this.RequireEmpty() =
        if not (this.pairs.Read().IsEmpty && this.targets.Read().IsEmpty && this.beginOnly.Read().IsEmpty)
           || this.pairs.Overflowed || this.targets.Overflowed || this.beginOnly.Overflowed then
            invalidOp "Suspended observer retained contact history."

    member this.OnDisable() =
        if not (obj.ReferenceEquals(this.pairs, null)) && not this.controller && not this.finished then
            this.RequireEmpty()
            this.disables <- this.disables + 1
            this.disabledCallbacks <- this.callbacks

    member this.OnEnable() =
        if not (obj.ReferenceEquals(this.pairs, null)) && not this.controller && not this.finished then
            this.RequireEmpty()
            if this.callbacks <> this.disabledCallbacks then invalidOp "Disabled observer callback ran."
            this.enables <- this.enables + 1

    member private this.Move(x: single) =
        this.Transform.WorldPosition <- Float3(x, 0.0f, 0.0f)
        if this.body.SetVelocity(Unchecked.defaultof<Float3>) <> PhysicsError.None then invalidOp "Controller move failed."

    member private this.Print(success, error: string) =
        let observer = if obj.ReferenceEquals(this.observer, null) then this else this.observer
        Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; success = success; error = error;
            subscriberLifetime = true; sensorBegin = this.begins; sensorPersist = this.persists; sensorEnd = this.ends;
            observerBegin = observer.begins; observerEnd = observer.ends; targetBegin = observer.targetBegins; targetEnd = observer.targetEnds;
            beginOnly = observer.beginOnlyCount; disables = observer.disables; enables = observer.enables; callbacksBeforeDisable = observer.disabledCallbacks;
            reactivatedPersists = observer.targetPersists - observer.enableTargetPersists; enableAfterTick = observer.enableAfterTick; seedTick = observer.seedTick |}).Substring(1))

    member this.PostPhysics(dt: single) =
        if not this.finished then
            try
                this.callbacks <- this.callbacks + 1
                this.elapsed <- this.elapsed + dt
                let values = this.pairs.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if not contact.Sensor || (contact.SelfShapeId <> 19u && contact.SelfShapeId <> 20u)
                       || contact.OtherShapeId <> 23u || contact.OtherEntity.Name <> "ContactHurt" || contact.Tick < this.lastTick then
                        invalidOp "Subscriber contact endpoint/tick mismatch."
                    this.lastTick <- contact.Tick
                    if not this.controller && this.enables > 0 && contact.Tick <= this.enableAfterTick then invalidOp "Disabled contact tick replayed."
                    if contact.Phase = ContactPhases.Begin then
                        this.begins <- this.begins + 1
                        if not this.controller && this.enables > 0 && this.seedTick = 0UL then this.seedTick <- contact.Tick
                    elif contact.Phase = ContactPhases.Persist then this.persists <- this.persists + 1
                    else this.ends <- this.ends + 1
                if not this.controller then
                    let values = this.targets.Read()
                    for i in 0 .. values.Length - 1 do
                        let contact = values[i]
                        if contact.Phase = ContactPhases.Begin then this.targetBegins <- this.targetBegins + 1
                        elif contact.Phase = ContactPhases.Persist then this.targetPersists <- this.targetPersists + 1
                        else this.targetEnds <- this.targetEnds + 1
                    let values = this.beginOnly.Read()
                    for i in 0 .. values.Length - 1 do
                        if values[i].Phase <> ContactPhases.Begin then invalidOp "Begin-only phase mismatch."
                        this.beginOnlyCount <- this.beginOnlyCount + 1
                    if this.begins > 4 || this.targetBegins > 2 || this.beginOnlyCount > 4 || this.ends > 2 || this.targetEnds > 1 then
                        invalidOp "Observer replayed history or overlap Begin."
                else
                    let observer = this.observer
                    if this.stage = 0 && observer.begins = 2 && observer.persists >= 2 then
                        observer.Enabled <- false
                        if observer.Enabled || observer.disables <> 1 then invalidOp "Native Disable dispatch missing."
                        observer.RequireEmpty()
                        this.Move(10.0f)
                        this.stageAt <- this.elapsed
                        this.stage <- 1
                    elif this.stage = 1 && this.ends = 2 && this.elapsed - this.stageAt > 0.15f then
                        observer.RequireEmpty()
                        if observer.callbacks <> observer.disabledCallbacks then invalidOp "Disabled PostPhysics ran."
                        this.Move(0.0f)
                        this.stage <- 2
                        this.stageAt <- this.elapsed
                    elif this.stage = 2 && this.begins = 4 && this.elapsed - this.stageAt > 0.15f then
                        observer.RequireEmpty()
                        observer.enableAfterTick <- this.lastTick
                        observer.enableTargetPersists <- observer.targetPersists
                        observer.Enabled <- true
                        if not observer.Enabled || observer.enables <> 1 then invalidOp "Native Enable dispatch missing."
                        observer.RequireEmpty()
                        this.stage <- 3
                    elif this.stage = 3 && observer.begins = 4 && observer.targetBegins = 2 && observer.beginOnlyCount = 4 && observer.targetPersists > observer.enableTargetPersists then
                        if observer.ends <> 0 || observer.targetEnds <> 0 || observer.seedTick <= observer.enableAfterTick then invalidOp "Missed disabled End was replayed."
                        this.Move(10.0f)
                        this.stage <- 4
                    elif this.stage = 4 && this.ends = 4 && observer.ends = 2 && observer.targetEnds = 1 then
                        this.stage <- 5
                        this.stageAt <- this.elapsed
                    elif this.stage = 5 && this.elapsed - this.stageAt > 0.1f then
                        if observer.begins <> 4 || observer.targetBegins <> 2 || observer.beginOnlyCount <> 4 || observer.ends <> 2
                           || observer.targetEnds <> 1 || this.begins <> 4 || this.ends <> 4 then invalidOp "Reactivated events replayed after final exit."
                        observer.finished <- true
                        this.finished <- true
                        this.Print(true, "")
                    if this.elapsed > 15.0f then invalidOp "Subscriber lifetime product timeout."
            with error ->
                this.finished <- true
                this.Print(false, error.Message)
                reraise()
