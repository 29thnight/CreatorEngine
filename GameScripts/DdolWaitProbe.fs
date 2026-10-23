namespace CreatorEngine.Scripts

open CreatorEngine

type DdolWaitProbe() =
    inherit Component()

    let mutable updates = 0
    let mutable elapsed = 0.0f
    let mutable reported = false
    let mutable dtReported = false

    member private _.Mark(point) =
        Component.Log($"[LC7c] point={point} frame={Component.FrameCount}")

    member _.PostPhysics(dt: single) : unit =
        if not dtReported then
            dtReported <- true
            let frames = if dt > 0.0f then 3.0f / dt else -1.0f
            let deltaText = dt.ToString("0.#####")
            let frameText = frames.ToString("0")
            Component.Log($"[LC7c] point=dt value={deltaText} cross=3 frames≈{frameText}")

    member this.OnInitialized() : unit = this.Mark("init")
    member this.OnAddedToScene() : unit = this.Mark("added")
    member this.OnBeginSimulation() : unit = this.Mark("begin")
    member this.OnRemovingFromScene() : unit = this.Mark("removing")
    member this.OnEndSimulation() : unit = this.Mark("end")
    member this.OnUninitializing() : unit = this.Mark("uninit")

    member this.OnTickSimulate(dt: single) : unit =
        if updates = 0 then
            this.Mark("update")

        updates <- updates + 1
        elapsed <- elapsed + dt

        if not reported && elapsed >= 3.0f then
            reported <- true
            this.Mark($"resumed name={this.Entity.Name}")
