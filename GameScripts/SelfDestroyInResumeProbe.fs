namespace CreatorEngine.Scripts

open System
open CreatorEngine

type SelfDestroyInResumeProbe() =
    inherit Component()

    let mutable elapsed = 0.0f
    let mutable acted = false
    let mutable ticks = 0

    member private _.Mark(point: string) =
        Component.Log(
            $"[LC0] kind=resume point={point} frame={Component.FrameCount} tid={Environment.CurrentManagedThreadId}"
        )

    member this.OnEndSimulation() : unit = this.Mark("end")
    member this.OnEnable() : unit = this.Mark("enable")
    member this.OnBeginSimulation() : unit = this.Mark("beginhook")
    member this.OnRemovingFromScene() : unit = this.Mark("removing")
    member this.OnDisable() : unit = this.Mark("disable")
    member this.OnUninitializing() : unit = this.Mark("uninit")
    member this.OnAddedToScene() : unit = this.Mark("added")
    member this.OnInitialized() : unit = this.Mark("init")

    member this.PrePhysics(_: single) : unit =
        if ticks < 6 then
            this.Mark("pre")

        ticks <- ticks + 1

    member this.OnTickSimulate(dt: single) : unit =
        elapsed <- elapsed + dt

        if not acted && elapsed >= 0.2f then
            acted <- true
            this.Mark("update")
            this.Entity.Destroy()
            this.Mark("acted")
