namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsContactFaultProbe() =
    inherit Component()
    static let mutable faultCalls = 0
    static let mutable faultDisables = 0
    static let mutable required = 0
    static let mutable readRejected = false
    let mutable contacts = Unchecked.defaultof<ContactStream>
    let mutable faultOwner = false
    let mutable finished = false
    let mutable elapsed = 0.0f
    let mutable healthyContacts = 0
    [<DefaultValue; SerializeField>] val mutable public _overflow: bool

    member this.OnBeginSimulation() =
        faultOwner <- this.Entity.Name = "ContactAttack"
        if faultOwner then
            faultCalls <- 0
            faultDisables <- 0
            required <- 0
            readRejected <- false
        contacts <- Physics.ObserveContacts(this.Entity, (if faultOwner then ShapeRoles.Attack else ShapeRoles.Hurt),
                        (if faultOwner then ShapeRoles.Hurt else ShapeRoles.Attack), ContactPhases.All,
                        (if faultOwner && this._overflow then 1 else 64))
        PhysicsContactStreamProbe.Track(contacts)

    member this.PostPhysics(dt: single) =
        if not finished then
            elapsed <- elapsed + dt
            if faultOwner then
                if contacts.RequiredCapacity <> 0 then
                    faultCalls <- faultCalls + 1
                    required <- contacts.RequiredCapacity
                    Console.WriteLine("{\"marker\":\"physics.contact.fault\"," + JsonSerializer.Serialize({| overflow = this._overflow; overflowed = contacts.Overflowed; required = required |}).Substring(1))
                    try
                        let _ = contacts.Read().Length
                        ()
                    with :? InvalidOperationException as error when this._overflow && error.Message = "Contact stream capacity exceeded; partial gameplay results are unavailable." ->
                        readRejected <- true
                        reraise()
                    if this._overflow then invalidOp "Overflow Read unexpectedly returned partial contacts."
                    invalidOp "PhysicsContactFaultProbe injected PostPhysics exception."
            else
                healthyContacts <- healthyContacts + contacts.Read().Length
                if elapsed >= 0.3f then
                    let failed = Entity.Find("ContactAttack").GetComponent<PhysicsContactFaultProbe>()
                    if obj.ReferenceEquals(failed, null) || failed.Enabled || faultCalls <> 1 || faultDisables <> 1
                       || healthyContacts < 4 || (this._overflow && (required < 2 || not readRejected)) then
                        invalidOp "Contact fault isolation incomplete."
                    finished <- true
                    Console.WriteLine("{\"marker\":\"physics.contact.stream\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; success = true; error = "";
                        contactFault = true; overflow = this._overflow; faultCalls = faultCalls; faultDisables = faultDisables;
                        readRejected = readRejected; required = required; healthyContacts = healthyContacts |}).Substring(1))

    member _.OnDisable() =
        if faultOwner then faultDisables <- faultDisables + 1

    member this.OnEndSimulation() =
        let disposed =
            try
                let _ = contacts.Read().Length
                ()
                false
            with :? ObjectDisposedException -> true
        Console.WriteLine("{\"marker\":\"physics.contact.fault.cleanup\"," + JsonSerializer.Serialize({| owner = this.Entity.Name; disposed = disposed |}).Substring(1))
