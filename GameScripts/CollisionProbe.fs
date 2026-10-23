namespace CreatorEngine.Scripts

open CreatorEngine

type CollisionProbe() =
    inherit Component()

    let mutable contacts = Unchecked.defaultof<ContactStream>

    [<DefaultValue; SerializeField>]
    val mutable public _logStay: bool

    member this.OnBeginSimulation() : unit =
        contacts <- Physics.ObserveContacts(this.Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All)

    member this.PostPhysics(_: single) : unit =
        if not (obj.ReferenceEquals(contacts, null)) then
            let values = contacts.Read()

            for i in 0 .. values.Length - 1 do
                let contact = values[i]

                if contact.Phase <> ContactPhases.Persist || this._logStay then
                    Component.Log(
                        $"[CollisionProbe] {contact.Phase} sensor={contact.Sensor} tick={contact.Tick} shape={contact.SelfShapeId}/{contact.OtherShapeId}"
                    )
