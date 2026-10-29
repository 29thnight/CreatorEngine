namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsLateContactProbe() =
    inherit Component()
    let mutable all = Unchecked.defaultof<ContactStream>
    let mutable beginOnly = Unchecked.defaultof<ContactStream>
    let mutable begins = 0
    let mutable persists = 0
    let mutable beginOnlyCount = 0
    let mutable firstTick = 0UL
    let mutable lastTick = 0UL
    let mutable finished = false

    member _.OnBeginSimulation() =
        if not PhysicsContactStreamProbe.AttackOwner.IsAlive || PhysicsContactStreamProbe.ObserverSpawnTick = 0UL then
            invalidOp "Observer started before active sensor pair."
        all <- Physics.ObserveContacts(PhysicsContactStreamProbe.AttackOwner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 16)
        beginOnly <- Physics.ObserveContacts(PhysicsContactStreamProbe.AttackOwner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.Begin, 16)
        PhysicsContactStreamProbe.Track(all)
        PhysicsContactStreamProbe.Track(beginOnly)

    member private _.Print(success, error: string) =
        Console.WriteLine("{\"marker\":\"physics.contact.late\"," + JsonSerializer.Serialize({| success = success; error = error;
            spawnTick = PhysicsContactStreamProbe.ObserverSpawnTick; firstTick = firstTick; lastTick = lastTick;
            begins = begins; persists = persists; beginOnly = beginOnlyCount |}).Substring(1))

    member this.PostPhysics(_: single) =
        if not finished then
            try
                let values = all.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if not contact.Sensor || contact.SelfShapeId <> 19u || contact.OtherShapeId <> 23u
                       || contact.SelfEntity <> PhysicsContactStreamProbe.AttackOwner || contact.OtherEntity.Name <> "ContactHurt"
                       || contact.Tick <= PhysicsContactStreamProbe.ObserverSpawnTick || contact.Tick < lastTick then
                        invalidOp "Late overlap endpoint/tick mismatch."
                    if firstTick = 0UL then firstTick <- contact.Tick
                    lastTick <- contact.Tick
                    if contact.Phase = ContactPhases.Begin then begins <- begins + 1
                    elif contact.Phase = ContactPhases.Persist then persists <- persists + 1
                    else invalidOp "Sensor ended before observer acceptance."
                let values = beginOnly.Read()
                for i in 0 .. values.Length - 1 do
                    let contact = values[i]
                    if not contact.Sensor || contact.Phase <> ContactPhases.Begin || contact.Tick <> firstTick then
                        invalidOp "Begin-only late overlap mismatch."
                    beginOnlyCount <- beginOnlyCount + 1
                if begins > 1 || beginOnlyCount > 1 then invalidOp "Late overlap Begin replayed."
                if persists >= 2 then
                    if begins <> 1 || beginOnlyCount <> 1 then invalidOp "Late overlap initial Begin missing."
                    finished <- true
                    PhysicsContactStreamProbe.LateReady <- true
                    this.Print(true, "")
            with error ->
                finished <- true
                this.Print(false, error.Message)
                reraise()
