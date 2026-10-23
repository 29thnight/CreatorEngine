namespace CreatorEngine.Scripts

open System
open CreatorEngine

type PhysicsSceneContactProbe() =
    inherit Component()

    let mutable contacts = Unchecked.defaultof<ContactStream>
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable previousTarget = Unchecked.defaultof<Entity>
    let mutable added = 0
    let mutable begins = 0
    let mutable persists = 0
    static let mutable current: PhysicsSceneContactProbe option = None
    static let mutable runs = 0
    static let streams = ResizeArray<ContactStream>()

    [<DefaultValue; SerializeField>]
    val mutable public _ready: bool

    member _.OnAddedToScene() : unit = added <- added + 1

    member this.OnBeginSimulation() : unit =
        this._ready <- false
        runs <- runs + 1
        current <- Some this
        body <- this.GetComponent<PhysicsBodyComponent>()

        if obj.ReferenceEquals(body, null) then
            invalidOp "Missing scene body."

        contacts <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 32)
        streams.Add(contacts)

    member this.OnRemovingFromScene() : unit =
        if not (obj.ReferenceEquals(contacts, null)) then
            let alive =
                try
                    if not (contacts.Read().IsEmpty) then
                        invalidOp "Old scene contacts retained at detach."

                    true
                with :? ObjectDisposedException ->
                    false

            if alive then
                begins <- 0
                persists <- 0
                this._ready <- false

    member this.PostPhysics(_: single) : unit =
        if not this._ready then
            let mutable state = Unchecked.defaultof<PhysicsBodyState>

            if body.ReadState(&state) <> PhysicsError.None then
                invalidOp "Captured body wrapper did not survive the scene boundary."

            let mutable target = Unchecked.defaultof<Entity>
            let values = contacts.Read()

            for i in 0 .. values.Length - 1 do
                let contact = values[i]

                if
                    not contact.Sensor
                    || contact.SelfEntity <> this.Entity
                    || contact.SelfComponentId <> body.ComponentId
                    || contact.OtherEntity.Name <> "ContactHurt"
                then
                    invalidOp "Scene contact endpoint mismatch."

                target <- contact.OtherEntity

                if added > 1 && target = previousTarget then
                    invalidOp "Old scene endpoint delivered after DDOL."

                if contact.Phase = ContactPhases.Begin then
                    begins <- begins + 1
                elif contact.Phase = ContactPhases.Persist then
                    persists <- persists + 1

            if begins = 2 && persists >= 2 then
                previousTarget <- target
                this._ready <- true
                Console.WriteLine("[physics.contact.scene] " + PhysicsSceneContactProbe.State())

    member _.OnEndSimulation() : unit =
        Console.WriteLine("[physics.contact.scene.end] " + PhysicsSceneContactProbe.State())

    member private this.InstanceState() =
        let ready = if this._ready then "true" else "false"

        let bodyComponentId =
            if obj.ReferenceEquals(body, null) then
                0UL
            else
                body.ComponentId

        $"\"ready\":{ready},\"added\":{added},\"begins\":{begins},\"persists\":{persists},\"bodyComponent\":{bodyComponentId}"

    [<EngineCallable>]
    static member State() =
        let mutable disposed = 0

        for stream in streams do
            try
                stream.Read().IsEmpty |> ignore
            with :? ObjectDisposedException ->
                disposed <- disposed + 1

        let instance =
            match current with
            | Some value -> value.InstanceState()
            | None -> "\"ready\":false,\"added\":0,\"begins\":0,\"persists\":0,\"bodyComponent\":0"

        "{"
        + instance
        + $",\"runs\":{runs},\"streams\":{streams.Count},\"disposed\":{disposed}}}"
