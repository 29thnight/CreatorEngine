namespace CreatorEngine.Scripts

open CreatorEngine

type GenerationProbe() =
    inherit Component()

    static let mutable created = 0
    static let mutable recordedHooks = 0
    let mutable updates = 0

    let id =
        created <- created + 1
        created

    [<EngineCallable>]
    static member RecordedHooks() = recordedHooks

    member private _.Mark(point) =
        recordedHooks <- recordedHooks + 1
        Component.Log($"[LC7] id={id} point={point} frame={Component.FrameCount}")

    member this.OnInitialized() : unit = this.Mark("init")
    member this.OnAddedToScene() : unit = this.Mark("added")
    member this.OnEnable() : unit = this.Mark("enable")
    member this.OnBeginSimulation() : unit = this.Mark("begin")
    member this.OnDisable() : unit = this.Mark("disable")
    member this.OnEndSimulation() : unit = this.Mark("end")
    member this.OnRemovingFromScene() : unit = this.Mark("removing")
    member this.OnUninitializing() : unit = this.Mark("uninit")

    member this.OnTickSimulate(dt: single) : unit =
        if updates = 0 then
            this.Mark("update")

        updates <- updates + 1

        if dt <= 0.0f || abs (dt - 1.0f / 60.0f) > 1e-6f then
            invalidOp "Wrong fixed dt"
