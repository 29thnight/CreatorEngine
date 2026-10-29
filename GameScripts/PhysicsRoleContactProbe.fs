namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsRoleContactProbe() =
    inherit Component()
    static let alternateRole = ShapeRole(Guid("c84b601f-9ca1-4ad9-a043-3a235833559b"))
    let mutable attack = Unchecked.defaultof<ContactStream>
    let mutable alternate = Unchecked.defaultof<ContactStream>
    let mutable attackTargets = Unchecked.defaultof<ContactStream>
    let mutable alternateTargets = Unchecked.defaultof<ContactStream>
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let identities = Array.zeroCreate<uint64> 3
    let begins = Array.zeroCreate<int> 3
    let persists = Array.zeroCreate<int> 3
    let ends = Array.zeroCreate<int> 3
    let mutable alternateBegin = 0
    let mutable alternatePersist = 0
    let mutable alternateEnd = 0
    let mutable targetBegin = 0
    let mutable targetEnd = 0
    let mutable alternateTargetBegin = 0
    let mutable alternateTargetEnd = 0
    let mutable stage = 0
    let mutable finished = false
    let mutable elapsed = 0.0f
    let mutable settledAt = 0.0f
    [<DefaultValue; SerializeField>] val mutable public _explicitBinding: bool

    member private _.Require(error: PhysicsError) =
        if error <> PhysicsError.None then invalidOp $"Role mutation: {error}"

    member private _.CheckRole(role: ShapeRole) =
        let mutable shape = Unchecked.defaultof<PhysicsShapeState>
        let mutable unchanged = Unchecked.defaultof<PhysicsShapeState>
        if body.GetShape(1, &shape) <> PhysicsError.None || shape.ShapeId <> 19u || shape.ContactRole <> role
           || not shape.Sensor || not shape.QueryEnabled || body.GetShape(2, &unchanged) <> PhysicsError.None
           || unchanged.ContactRole <> ShapeRoles.Attack then
            invalidOp "Role mutation changed flags/sibling or Play did not restore role."

    member private this.Observe(role: ShapeRole, grouping: ContactGrouping) =
        let stream = Physics.ObserveContacts(this.Entity, role, ShapeRoles.Hurt, ContactPhases.All, 64, grouping)
        PhysicsContactStreamProbe.Track(stream)
        stream

    member this.OnBeginSimulation() =
        body <- this.GetComponent<PhysicsBodyComponent>()
        if obj.ReferenceEquals(body, null) then invalidOp "Missing role body."
        this.CheckRole(ShapeRoles.Attack)
        if this._explicitBinding then
            this.Require(body.SetShapeRole(19u, Unchecked.defaultof<ShapeRole>))
            this.CheckRole(Unchecked.defaultof<ShapeRole>)
            Physics.BindContactRole(body, 19u, ShapeRoles.Attack)
            let duplicateRejected =
                try
                    Physics.BindContactRole(body, 19u, ShapeRoles.Attack)
                    false
                with :? InvalidOperationException -> true
            if not duplicateRejected then invalidOp "Duplicate scoped binding accepted."
        attack <- this.Observe(ShapeRoles.Attack, ContactGrouping.ShapePairs)
        alternate <- this.Observe(alternateRole, ContactGrouping.ShapePairs)
        attackTargets <- this.Observe(ShapeRoles.Attack, ContactGrouping.SensorTargets)
        alternateTargets <- this.Observe(alternateRole, ContactGrouping.SensorTargets)

    member private this.ReadPairs(stream: ContactStream, isAlternate: bool) =
        let values = stream.Read()
        for i in 0 .. values.Length - 1 do
            let contact = values[i]
            if not contact.Sensor || contact.SelfEntity <> this.Entity || contact.SelfComponentId <> body.ComponentId
               || contact.OtherEntity.Name <> "ContactHurt" || contact.OtherShapeId <> 23u
               || (contact.SelfShapeId <> 19u && contact.SelfShapeId <> 20u) then invalidOp "Role endpoint mismatch."
            let identity = (uint64 contact.SelfBodySlot <<< 32) ||| uint64 contact.SelfBodyGeneration
            let mutable index = Array.IndexOf(identities, identity)
            if index < 0 then
                if stage > 2 || identities[stage] <> 0UL then invalidOp "Unexpected role body identity."
                index <- stage
                identities[index] <- identity
            if contact.Phase <> ContactPhases.End && index <> stage then invalidOp "Retired role produced stale contact."
            if isAlternate <> (index = 1 && contact.SelfShapeId = 19u) then
                invalidOp "Retired endpoint role snapshot lost or new role misrouted."
            if isAlternate then
                if contact.Phase = ContactPhases.Begin then alternateBegin <- alternateBegin + 1
                elif contact.Phase = ContactPhases.Persist then alternatePersist <- alternatePersist + 1
                else alternateEnd <- alternateEnd + 1
            else
                if contact.Phase = ContactPhases.Begin then begins[index] <- begins[index] + 1
                elif contact.Phase = ContactPhases.Persist then persists[index] <- persists[index] + 1
                else ends[index] <- ends[index] + 1

    member private this.Print(success, error: string) =
        Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; success = success; error = error;
            roleTransition = true; explicitBinding = this._explicitBinding; finalRoleChanged = finished && success;
            attackBegins = begins; attackPersists = persists; attackEnds = ends; bodies = identities;
            alternateBegin = alternateBegin; alternatePersist = alternatePersist; alternateEnd = alternateEnd;
            targetBegin = targetBegin; targetEnd = targetEnd; alternateTargetBegin = alternateTargetBegin; alternateTargetEnd = alternateTargetEnd |}).Substring(1))

    member this.PostPhysics(dt: single) =
        if not finished then
            try
                elapsed <- elapsed + dt
                this.ReadPairs(attack, false)
                this.ReadPairs(alternate, true)
                let values = attackTargets.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if contact.Phase = ContactPhases.Begin then targetBegin <- targetBegin + 1
                    elif contact.Phase = ContactPhases.End then targetEnd <- targetEnd + 1
                let values = alternateTargets.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if contact.Phase = ContactPhases.Begin then alternateTargetBegin <- alternateTargetBegin + 1
                    elif contact.Phase = ContactPhases.End then alternateTargetEnd <- alternateTargetEnd + 1
                if stage = 0 && begins[0] = 2 && persists[0] >= 2 then
                    if body.SetShapeRole(9999u, alternateRole) <> PhysicsError.StaleHandle then invalidOp "Unknown shape role mutation accepted."
                    this.Require(body.SetShapeRole(19u, alternateRole))
                    this.CheckRole(alternateRole)
                    stage <- 1
                elif stage = 1 && ends[0] = 2 && begins[1] = 1 && persists[1] > 0 && alternateBegin = 1 && alternatePersist > 0 then
                    let role = if this._explicitBinding then Unchecked.defaultof<ShapeRole> else ShapeRoles.Attack
                    this.Require(body.SetShapeRole(19u, role))
                    this.CheckRole(role)
                    stage <- 2
                elif stage = 2 && ends[1] = 1 && alternateEnd = 1 && begins[2] = 2 && persists[2] >= 2 then
                    this.Transform.WorldPosition <- Float3(10.0f, 0.0f, 0.0f)
                    this.Require(body.SetVelocity(Unchecked.defaultof<Float3>))
                    stage <- 3
                elif stage = 3 && ends[2] = 2 then
                    stage <- 4
                    settledAt <- elapsed
                elif stage = 4 && elapsed - settledAt > 0.1f then
                    if begins <> [|2; 1; 2|] || ends <> [|2; 1; 2|] || alternateBegin <> 1 || alternateEnd <> 1
                       || alternateTargetBegin <> 1 || alternateTargetEnd <> 1 || targetBegin < 1 || targetBegin > 3
                       || targetBegin <> targetEnd || (Array.distinct identities).Length <> 3 then
                        invalidOp "Role transition acceptance incomplete."
                    let role = if this._explicitBinding then Unchecked.defaultof<ShapeRole> else alternateRole
                    this.Require(body.SetShapeRole(19u, role))
                    this.CheckRole(role)
                    finished <- true
                    this.Print(true, "")
                if elapsed > 15.0f then invalidOp "Role transition timeout."
            with error ->
                finished <- true
                this.Print(false, error.Message)
                reraise()
