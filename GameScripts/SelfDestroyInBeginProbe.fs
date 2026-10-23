namespace CreatorEngine.Scripts

open System
open CreatorEngine

type SelfDestroyInBeginProbe() =
    inherit Component()

    let mutable ticks = 0

    member private _.Mark(point: string) =
        Component.Log(
            $"[LC0] kind=begin point={point} frame={Component.FrameCount} tid={Environment.CurrentManagedThreadId}"
        )

    member this.OnInitialized() : unit = this.Mark("init")
    member this.OnAddedToScene() : unit = this.Mark("added")
    member this.OnEnable() : unit = this.Mark("enable")
    member this.OnDisable() : unit = this.Mark("disable")
    member this.OnEndSimulation() : unit = this.Mark("end")
    member this.OnRemovingFromScene() : unit = this.Mark("removing")
    member this.OnUninitializing() : unit = this.Mark("uninit")

    member this.OnBeginSimulation() : unit =
        this.Mark("beginhook")
        this.Entity.Destroy()
        this.Mark("destroyed")

    member this.OnTickSimulate(_: single) : unit = this.Mark("unexpected-update")

    member this.PostPhysics(_: single) : unit =
        if ticks < 3 then
            this.Mark("tick")

        ticks <- ticks + 1
