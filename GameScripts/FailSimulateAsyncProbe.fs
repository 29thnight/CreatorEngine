namespace CreatorEngine.Scripts

open System
open CreatorEngine

type FailSimulateAsyncProbe() =
    inherit Component()

    let mutable beats = 0
    let mutable elapsed = 0.0f

    member private this.Mark(hook: string) =
        Component.Log($"[LC1] kind=failsimasync hook={hook} owner={this.Entity.Name}")

    member this.OnInitialized() : unit = this.Mark("OnInitialized")
    member this.OnAddedToScene() : unit = this.Mark("OnAddedToScene")
    member this.OnEnable() : unit = this.Mark("OnEnable")
    member this.OnBeginSimulation() : unit = this.Mark("OnBeginSimulation")
    member this.OnDisable() : unit = this.Mark("OnDisable")
    member this.OnEndSimulation() : unit = this.Mark("OnEndSimulation")
    member this.OnRemovingFromScene() : unit = this.Mark("OnRemovingFromScene")
    member this.OnUninitializing() : unit = this.Mark("OnUninitializing")

    member this.PostPhysics(_: single) : unit =
        beats <- beats + 1

        if beats % 30 = 0 then
            this.Mark("beat")

    member this.OnTickSimulate(dt: single) : unit =
        elapsed <- elapsed + dt

        if elapsed >= 0.1f then
            this.Mark("OnTickSimulate")
            invalidOp "Delayed OnTickSimulate failure isolation probe"
