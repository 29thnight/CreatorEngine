namespace CreatorEngine.Scripts

open System
open CreatorEngine

type ReentrantDestroyProbe() =
    inherit Component()

    let mutable elapsed = 0.0f
    let mutable acted = false
    let mutable ticks = 0

    member private _.Mark(point: string) =
        Component.Log($"[LC4] kind=destroy point={point} tid={Environment.CurrentManagedThreadId}")

    member this.OnUninitializing() : unit = this.Mark("uninit")
    member this.OnBeginSimulation() : unit = this.Mark("begin")
    member this.OnRemovingFromScene() : unit = this.Mark("removing")
    member this.OnEndSimulation() : unit = this.Mark("end")

    member this.OnTickSimulate(dt: single) : unit =
        elapsed <- elapsed + dt

        if not acted && elapsed >= 0.2f then
            acted <- true
            this.Mark("update")
            this.Entity.Destroy()
            this.Mark("acted")
