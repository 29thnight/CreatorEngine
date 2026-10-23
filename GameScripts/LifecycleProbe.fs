namespace CreatorEngine.Scripts

open CreatorEngine

type LifecycleProbe() =
    inherit Component()

    let mutable updates = 0
    let mutable elapsed = 0.0f
    let mutable reported = false
    let mutable initializedFrame = 0UL

    member private this.Mark(hook) =
        Component.Log($"[Probe] {hook} — {this.Entity.Name} · 엔진프레임 {Component.FrameCount}")

    member this.OnInitialized() : unit =
        initializedFrame <- Component.FrameCount
        this.Mark("Initialized")

    member this.OnBeginSimulation() : unit =
        Component.Log(
            $"[Probe] BeginSimulation — {this.Entity.Name} · 엔진프레임 {Component.FrameCount} (Initialized는 {initializedFrame})"
        )

    member this.OnAddedToScene() : unit = this.Mark("AddedToScene")
    member this.OnRemovingFromScene() : unit = this.Mark("RemovingFromScene")
    member this.OnEndSimulation() : unit = this.Mark("EndSimulation")
    member this.OnUninitializing() : unit = this.Mark("Uninitializing")
    member this.OnEnable() : unit = this.Mark("Enable")
    member this.OnDisable() : unit = this.Mark("Disable")

    member this.OnTickSimulate(dt: single) : unit =
        if updates = 0 then
            Component.Log($"[Probe] UpdateStart — {this.Entity.Name} · dt={dt:R}")

        updates <- updates + 1
        elapsed <- elapsed + dt

        if not reported && elapsed >= 0.2f then
            reported <- true
            Component.Log($"[Probe] UpdateProgress — {this.Entity.Name} · updates={updates}")
