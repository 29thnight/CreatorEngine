namespace CreatorEngine.Scripts

open System
open CreatorEngine

type SelfDestroyInTickProbe() =
    inherit Component()

    let mutable ticks = 0

    member private _.Mark(point: string) =
        Component.Log(
            $"[LC0] kind=tick point={point} frame={Component.FrameCount} tid={Environment.CurrentManagedThreadId}"
        )

    member this.OnInitialized() : unit = this.Mark("init")
    member this.OnAddedToScene() : unit = this.Mark("added")
    member this.OnEnable() : unit = this.Mark("enable")
    member this.OnDisable() : unit = this.Mark("disable")
    member this.OnEndSimulation() : unit = this.Mark("end")
    member this.OnRemovingFromScene() : unit = this.Mark("removing")
    member this.OnUninitializing() : unit = this.Mark("uninit")
    member this.OnBeginSimulation() : unit = this.Mark("beginhook")

    member this.PostPhysics(_: single) : unit =
        ticks <- ticks + 1

        if ticks = 3 then
            this.Mark("destroying")
            this.Entity.Destroy()
            this.Mark("destroyed")
        elif ticks > 3 && ticks <= 6 then
            this.Mark("tick")
