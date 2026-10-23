namespace CreatorEngine.Scripts

open System
open CreatorEngine

type DisableInBeginProbe() =
    inherit Component()

    member private this.Mark(hook: string) =
        Component.Log($"[LC1] kind=disablebegin hook={hook} owner={this.Entity.Name}")

    member this.OnInitialized() : unit = this.Mark("OnInitialized")
    member this.OnAddedToScene() : unit = this.Mark("OnAddedToScene")
    member this.OnEnable() : unit = this.Mark("OnEnable")

    member this.OnBeginSimulation() : unit =
        this.Mark("OnBeginSimulation")
        this.Enabled <- false

    member this.OnDisable() : unit = this.Mark("OnDisable")
    member this.OnEndSimulation() : unit = this.Mark("OnEndSimulation")
    member this.OnRemovingFromScene() : unit = this.Mark("OnRemovingFromScene")
    member this.OnUninitializing() : unit = this.Mark("OnUninitializing")
    member this.OnTickSimulate(_: single) : unit = this.Mark("OnTickSimulate")
