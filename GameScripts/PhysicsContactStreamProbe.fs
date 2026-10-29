namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsContactStreamProbe() =
    inherit Component()

    static let streams = ResizeArray<ContactStream>()
    static let mutable started = 0
    static let mutable frames = 0
    static let mutable attackOwner = Unchecked.defaultof<Entity>
    static let mutable observerSpawnTick = 0UL
    static let mutable lateReady = false
    let mutable removedBody = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable retiredTarget = Unchecked.defaultof<Entity>
    let mutable retiredComponent = 0UL
    let mutable staleChecks = 0
    let mutable grouped = Unchecked.defaultof<ContactStream>
    let mutable contacts = Unchecked.defaultof<ContactStream>
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable attack = false
    let mutable finished = false
    let mutable stage = 0
    let mutable sensorBegin = 0
    let mutable sensorPersist = 0
    let mutable sensorEnd = 0
    let mutable begins = 0
    let mutable persists = 0
    let mutable ends = 0
    let mutable elapsed = 0.0f
    let mutable stageAt = 0.0f
    let mutable lastTick = 0UL
    let mutable groupBegin = 0
    let mutable groupPersist = 0
    let mutable groupEnd = 0
    let mutable groupTick = 0UL
    let mutable partialExit = false

    [<DefaultValue; SerializeField>] val mutable public _retireTarget: bool
    [<DefaultValue; SerializeField>] val mutable public _removeBodyOnly: bool
    [<DefaultValue; SerializeField>] val mutable public _groupTargets: bool
    [<DefaultValue; SerializeField>] val mutable public _lateObserverPrefab: string

    static member internal AttackOwner with get() = attackOwner and set value = attackOwner <- value
    static member internal ObserverSpawnTick with get() = observerSpawnTick and set value = observerSpawnTick <- value
    static member internal LateReady with get() = lateReady and set value = lateReady <- value
    static member internal Track(stream: ContactStream) = streams.Add(stream)

    member this.OnBeginSimulation() =
        started <- started + 1
        if this._removeBodyOnly then
            let restored = Entity.Find("ContactHurt").GetComponent<PhysicsBodyComponent>()
            let mutable shape = Unchecked.defaultof<PhysicsShapeState>
            if obj.ReferenceEquals(restored, null) || restored.GetShape(0, &shape) <> PhysicsError.None
               || shape.ShapeId <> 23u || shape.ContactRole <> ShapeRoles.Hurt then
                invalidOp "Removed body was not restored before Play."
        attack <- this.Entity.Name = "ContactAttack"
        if attack then
            attackOwner <- this.Entity
            observerSpawnTick <- 0UL
            lateReady <- false
        body <- this.GetComponent<PhysicsBodyComponent>()
        if obj.ReferenceEquals(body, null) then invalidOp "Missing contact body."
        let mutable authored = Unchecked.defaultof<PhysicsShapeState>
        if body.GetShape(0, &authored) <> PhysicsError.None
           || authored.ContactRole <> (if attack then ShapeRoles.Attack else ShapeRoles.Hurt) then
            invalidOp "Stored contact role missing."
        let bindingRejected =
            try
                Physics.BindContactRole(body, (if attack then 17u else 23u), ShapeRoles.Attack)
                false
            with :? InvalidOperationException -> true
        if not bindingRejected then invalidOp "Authored role accepted script rebinding."
        contacts <- Physics.ObserveContacts(this.Entity, (if attack then ShapeRoles.Attack else ShapeRoles.Hurt),
                        (if attack then ShapeRoles.Hurt else ShapeRoles.Attack), ContactPhases.All, 64)
        streams.Add(contacts)
        if this._groupTargets then
            grouped <- Physics.ObserveContacts(this.Entity, (if attack then ShapeRoles.Attack else ShapeRoles.Hurt),
                           (if attack then ShapeRoles.Hurt else ShapeRoles.Attack), ContactPhases.All, 64, ContactGrouping.SensorTargets)
            streams.Add(grouped)

    member private _.Require(error: PhysicsError, operation: string) =
        if error <> PhysicsError.None then invalidOp $"{operation}: {error}"

    member private _.ExpectStale(error: PhysicsError) =
        if error <> PhysicsError.StaleHandle then invalidOp $"Removed wrapper retargeted: {error}"
        staleChecks <- staleChecks + 1

    member private this.Print(success, error: string) =
        Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name;
            success = success; error = error; sensorBegin = sensorBegin; sensorPersist = sensorPersist; sensorEnd = sensorEnd;
            ``begin`` = begins; persist = persists; ``end`` = ends; tick = lastTick; componentRemoved = this._removeBodyOnly;
            staleChecks = staleChecks; retired = this._retireTarget; deletedAlive = retiredTarget.IsAlive;
            grouped = this._groupTargets; groupBegin = groupBegin; groupPersist = groupPersist; groupEnd = groupEnd |}).Substring(1))

    member private this.ReadRetirement(dt: single) =
        elapsed <- elapsed + dt
        let values = contacts.Read()
        for i in 0 .. values.Length - 1 do
            let contact = values[i]
            if not contact.Sensor || contact.SelfEntity <> this.Entity || contact.OtherShapeId <> 23u
               || (contact.SelfShapeId <> 19u && contact.SelfShapeId <> 20u) || contact.Tick = 0UL || contact.Tick < lastTick then
                invalidOp "Retired endpoint mismatch."
            lastTick <- contact.Tick
            if stage = 0 then
                retiredTarget <- contact.OtherEntity
                retiredComponent <- contact.OtherComponentId
                if contact.Phase = ContactPhases.Begin then sensorBegin <- sensorBegin + 1
                elif contact.Phase = ContactPhases.Persist then sensorPersist <- sensorPersist + 1
                else invalidOp "Early retirement End."
            else
                if contact.Phase <> ContactPhases.End || contact.OtherEntity <> retiredTarget
                   || contact.OtherComponentId <> retiredComponent || contact.OtherEntity.IsAlive <> this._removeBodyOnly then
                    invalidOp "Deleted identity lost or stale Persist delivered."
                sensorEnd <- sensorEnd + 1
        let groupedValues = grouped.Read()
        for i in 0 .. groupedValues.Length - 1 do
            let contact = groupedValues[i]
            if not contact.Sensor || contact.OtherEntity <> retiredTarget then invalidOp "Retired grouped endpoint mismatch."
            if contact.Phase = ContactPhases.Begin then groupBegin <- groupBegin + 1
            elif contact.Phase = ContactPhases.Persist then
                if stage <> 0 || contact.Tick <= groupTick then invalidOp "Retired grouped stale/duplicate Persist."
                groupPersist <- groupPersist + 1
            else groupEnd <- groupEnd + 1
            groupTick <- contact.Tick
        if stage = 0 && sensorBegin = 2 && sensorPersist > 0 && groupBegin = 1 && groupPersist > 0 then
            if this._removeBodyOnly then
                removedBody <- PhysicsBodyComponent.Find(retiredTarget, retiredComponent)
                if obj.ReferenceEquals(removedBody, null) then invalidOp "Removal target wrapper missing."
                this.Require(removedBody.Remove(), "remove exact body component")
                let mutable state = Unchecked.defaultof<PhysicsBodyState>
                this.ExpectStale(removedBody.ReadState(&state))
                this.ExpectStale(removedBody.SetVelocity(Unchecked.defaultof<Float3>))
                this.ExpectStale(removedBody.Remove())
            else retiredTarget.Destroy()
            stage <- 1
        elif stage = 1 && sensorEnd = 2 && groupEnd = 1 then
            stage <- 2
            stageAt <- elapsed
        elif stage = 2 && elapsed - stageAt > 0.1f then
            if sensorEnd <> 2 || groupEnd <> 1 || retiredTarget.IsAlive <> this._removeBodyOnly then
                invalidOp "Retired End replay or owner lifetime mismatch."
            if this._removeBodyOnly then
                if retiredTarget.HasComponent<PhysicsBodyComponent>() || not (obj.ReferenceEquals(PhysicsBodyComponent.Find(retiredTarget, retiredComponent), null)) then
                    invalidOp "Removed body still discoverable."
                let mutable state = Unchecked.defaultof<PhysicsBodyState>
                this.ExpectStale(removedBody.ReadState(&state))
                this.ExpectStale(removedBody.SetShapeFlags(23u, true, true))
                this.ExpectStale(removedBody.ApplyForce(Unchecked.defaultof<Float3>))
            finished <- true
            this.Print(true, "")
        if elapsed > 15.0f then invalidOp "Retirement product timeout."

    member private this.ReadContacts(dt: single) =
        elapsed <- elapsed + dt
        let values = contacts.Read()
        for i in 0 .. values.Length - 1 do
            let contact = values[i]
            if contact.SelfEntity <> this.Entity || contact.OtherEntity.Name <> (if attack then "ContactHurt" else "ContactAttack")
               || contact.SelfComponentId <> body.ComponentId || contact.OtherComponentId = 0UL
               || contact.Tick = 0UL || contact.Tick < lastTick
               || (if attack then contact.OtherShapeId <> 23u else contact.SelfShapeId <> 23u) then
                invalidOp "Contact endpoint/orientation/tick mismatch."
            let attackShape = if attack then contact.SelfShapeId else contact.OtherShapeId
            if attackShape <> (if contact.Sensor then 19u else 17u) && not (this._groupTargets && contact.Sensor && attackShape = 20u) then
                invalidOp "Contact shape mismatch."
            lastTick <- contact.Tick
            if contact.Sensor then
                if contact.Phase = ContactPhases.Begin then sensorBegin <- sensorBegin + 1
                elif contact.Phase = ContactPhases.Persist then sensorPersist <- sensorPersist + 1
                elif contact.Phase = ContactPhases.End then sensorEnd <- sensorEnd + 1
            else
                if contact.Phase = ContactPhases.Begin then begins <- begins + 1
                elif contact.Phase = ContactPhases.Persist then persists <- persists + 1
                elif contact.Phase = ContactPhases.End then ends <- ends + 1
        if not (obj.ReferenceEquals(grouped, null)) then
            let groupedValues = grouped.Read()
            for i in 0 .. groupedValues.Length - 1 do
                let contact = groupedValues[i]
                if contact.Sensor then
                    if contact.SelfEntity <> this.Entity || contact.OtherEntity.Name <> (if attack then "ContactHurt" else "ContactAttack") then
                        invalidOp "Grouped endpoint mismatch."
                    if contact.Phase = ContactPhases.Begin then groupBegin <- groupBegin + 1
                    elif contact.Phase = ContactPhases.End then groupEnd <- groupEnd + 1
                    else
                        if contact.Tick <= groupTick then invalidOp "Grouped Persist repeated per tick."
                        groupPersist <- groupPersist + 1
                    groupTick <- contact.Tick
            if groupBegin > 1 || groupEnd > 1 || (sensorEnd = 1 && groupEnd <> 0) then
                invalidOp "Grouped target transition repeated or ended on partial exit."
        if attack && stage = 0 && sensorBegin > 0 && sensorPersist > 0 then
            if not (String.IsNullOrEmpty(this._lateObserverPrefab)) && observerSpawnTick = 0UL then
                observerSpawnTick <- lastTick
                if not (Prefab.Load(this._lateObserverPrefab).Instantiate("LateContactObserver").IsAlive) then
                    invalidOp "Late observer prefab spawn failed."
            if String.IsNullOrEmpty(this._lateObserverPrefab) || lateReady then
                if this._groupTargets && not partialExit then
                    if sensorBegin = 2 then
                        this.Transform.WorldPosition <- Float3(0.6f, 0.0f, 0.0f)
                        this.Require(body.SetVelocity(Unchecked.defaultof<Float3>), "partial sensor exit")
                        partialExit <- true
                        stageAt <- elapsed
                elif not this._groupTargets || (sensorEnd = 1 && elapsed - stageAt >= 0.1f) then
                    this.Transform.WorldPosition <- Float3(2.5f, 0.0f, 0.0f)
                    this.Require(body.SetVelocity(Unchecked.defaultof<Float3>), "enter solid pair")
                    stage <- 1
                    stageAt <- elapsed
        elif attack && stage = 1 && persists > 0 && elapsed - stageAt > 0.1f then
            this.Transform.WorldPosition <- Float3(10.0f, 0.0f, 0.0f)
            this.Require(body.SetVelocity(Unchecked.defaultof<Float3>), "leave solid pair")
            stage <- 2
        if sensorBegin > 0 && sensorPersist > 0 && sensorEnd > 0 && begins > 0 && persists > 0 && ends > 0 then
            if this._groupTargets && (sensorBegin <> 2 || sensorEnd <> 2 || groupBegin <> 1 || groupPersist < 2 || groupEnd <> 1) then
                invalidOp "Grouped target acceptance incomplete."
            finished <- true
            this.Print(true, "")
        elif elapsed > 15.0f then invalidOp "Contact stream product timeout."

    member this.PostPhysics(dt: single) =
        frames <- frames + 1
        if not finished then
            try
                if this._retireTarget then this.ReadRetirement(dt) else this.ReadContacts(dt)
            with error ->
                finished <- true
                this.Print(false, error.Message)
                reraise()

    [<EngineCallable>]
    static member State() = JsonSerializer.Serialize({| started = started; frames = frames |})

    [<EngineCallable>]
    static member Retired() =
        let mutable disposed = 0
        for stream in streams do
            try
                let _ = stream.Read().Length
                ()
            with :? ObjectDisposedException -> disposed <- disposed + 1
        JsonSerializer.Serialize({| streams = streams.Count; disposed = disposed |})
