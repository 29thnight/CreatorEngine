namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsSolidContactProbe() =
    inherit Component()
    let mutable pairs = Unchecked.defaultof<ContactStream>
    let mutable targets = Unchecked.defaultof<ContactStream>
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let begins = Array.zeroCreate<int> 2
    let persists = Array.zeroCreate<int> 2
    let ends = Array.zeroCreate<int> 2
    let mutable stage = 0
    let mutable partialPersists = 0
    let mutable elapsed = 0.0f
    let mutable settled = 0.0f
    let mutable finished = false

    member this.OnBeginSimulation() =
        body <- this.GetComponent<PhysicsBodyComponent>()
        if obj.ReferenceEquals(body, null) then invalidOp "Missing solid body."
        pairs <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 128)
        targets <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 128, ContactGrouping.SensorTargets)
        PhysicsContactStreamProbe.Track(pairs)
        PhysicsContactStreamProbe.Track(targets)

    member private this.Print(success, error: string) =
        Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; success = success; error = error;
            solidContacts = true; begins = begins; persists = persists; ends = ends; partialPersists = partialPersists; stage = stage |}).Substring(1))

    member this.PostPhysics(dt: single) =
        if not finished then
            try
                elapsed <- elapsed + dt
                let pairValues = pairs.Read()
                let targetValues = targets.Read()
                if pairValues.Length <> targetValues.Length then invalidOp "Solid target grouping lost pairs."
                for i in 0 .. pairValues.Length - 1 do
                    let contact = pairValues[i]
                    let grouped = targetValues[i]
                    let shape = if contact.SelfShapeId = 19u then 0 elif contact.SelfShapeId = 20u then 1 else -1
                    if shape < 0 || contact.Sensor || contact.SelfEntity <> this.Entity || contact.OtherEntity.Name <> "ContactHurt"
                       || contact.SelfComponentId <> body.ComponentId || contact.OtherShapeId <> 23u || contact.Tick = 0UL
                       || grouped.SelfShapeId <> contact.SelfShapeId || grouped.Phase <> contact.Phase || grouped.Tick <> contact.Tick
                       || grouped.ContactCount <> contact.ContactCount || grouped.RequiredContacts <> contact.RequiredContacts then
                        invalidOp "Solid endpoint or grouped payload mismatch."
                    if contact.Phase = ContactPhases.Begin then begins[shape] <- begins[shape] + 1
                    elif contact.Phase = ContactPhases.End then ends[shape] <- ends[shape] + 1
                    else
                        persists[shape] <- persists[shape] + 1
                        if stage = 1 && shape = 0 && ends[1] = 1 then partialPersists <- partialPersists + 1
                if stage = 0 && begins[0] = 1 && begins[1] = 1 && persists[0] >= 2 && persists[1] >= 2 then
                    this.Transform.WorldPosition <- Float3(0.0f, 0.0f, 1.5f)
                    stage <- 1
                elif stage = 1 && ends[1] = 1 && ends[0] = 0 && partialPersists >= 2 then
                    this.Transform.WorldPosition <- Float3(0.0f, 0.0f, 5.0f)
                    stage <- 2
                elif stage = 2 && ends[0] = 1 then
                    stage <- 3
                    settled <- elapsed
                elif stage = 3 && elapsed - settled > 0.1f then
                    if begins[0] <> 1 || begins[1] <> 1 || ends[0] <> 1 || ends[1] <> 1 || not pairValues.IsEmpty then
                        invalidOp "Solid counts or stale replay mismatch."
                    finished <- true
                    this.Print(true, "")
                if elapsed > 15.0f then invalidOp "Solid contact timeout."
            with error ->
                finished <- true
                this.Print(false, error.Message)
                reraise()
