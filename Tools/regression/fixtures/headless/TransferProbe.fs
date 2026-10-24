namespace CreatorEngine.Scripts

open System
open CreatorEngine

/// Exercises the ordinary native DDOL transfer without reconstructing the script.
type TransferProbe() =
    inherit Component()

    static let mutable initialized = 0
    static let mutable added = 0
    static let mutable removing = 0
    static let mutable began = 0
    static let mutable ended = 0
    static let mutable retired = 0
    static let mutable ticks = 0
    static let mutable failures = 0

    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>

    member _.OnInitialized() : unit = initialized <- initialized + 1

    member this.OnAddedToScene() : unit =
        added <- added + 1
        if not this.Entity.IsAlive || this.Entity.Name <> "Mover" then
            failures <- failures + 1
            invalidOp "DDOL script owner handle was lost"

    member this.OnBeginSimulation() : unit =
        began <- began + 1
        body <- this.GetComponent<PhysicsBodyComponent>()

    member _.OnTickSimulate(_: single) : unit =
        ticks <- ticks + 1
        if body.SetVelocity(Float3(3.0f, 0.0f, 0.0f)) <> PhysicsError.None then
            failures <- failures + 1
            invalidOp "DDOL body wrapper lost its native binding"

    member this.OnRemovingFromScene() : unit =
        removing <- removing + 1
        // IsAlive becomes false when destruction is requested. The handle
        // must still resolve identity until the final native cleanup hook.
        if this.Entity.Name <> "Mover" then
            failures <- failures + 1
            invalidOp "Owner handle was retired before the final hook"

    member _.OnEndSimulation() : unit = ended <- ended + 1
    member _.OnUninitializing() : unit = retired <- retired + 1

    [<EngineCallable>]
    static member State() =
        $"{{\"ticks\":{ticks},\"initialized\":{initialized},\"added\":{added},\"removing\":{removing},\"began\":{began},\"ended\":{ended},\"retired\":{retired},\"failures\":{failures}}}"
