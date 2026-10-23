namespace CreatorEngine.Scripts

open CreatorEngine

type LifecycleAxisProbe() =
    inherit Component()

    static let mutable created = 0

    let ordinal =
        created <- created + 1
        created

    let mutable fixedCount = 0
    let mutable pre = 0
    let mutable post = 0

    [<EngineCallable>]
    member this.SetActive(active: bool) = this.Entity.SetEnabled(active)

    member private _.Emit(kind, name) =
        Component.Log($"[Axis] #{ordinal} kind={kind} name={name} frame={Component.FrameCount} pre={pre} post={post}")

    member this.OnInitialized() : unit = this.Emit("hook", "OnInitialized")
    member this.OnAddedToScene() : unit = this.Emit("hook", "OnAddedToScene")
    member this.OnBeginSimulation() : unit = this.Emit("hook", "OnBeginSimulation")
    member this.OnEndSimulation() : unit = this.Emit("hook", "OnEndSimulation")

    member this.OnRemovingFromScene() : unit =
        this.Emit("hook", "OnRemovingFromScene")

    member this.OnUninitializing() : unit =
        this.Emit("hook", "OnUninitializing")

        this.Emit(
            "mark",
            if this.Entity.Name.Length > 0 then
                "ownerreachable"
            else
                "ownerlost"
        )

    member this.OnEnable() : unit = this.Emit("hook", "OnEnable")
    member this.OnDisable() : unit = this.Emit("hook", "OnDisable")

    member this.PrePhysics(_: single) : unit =
        pre <- pre + 1

        if pre = 1 then
            this.Emit("tick", "prefirst")
        elif pre % 30 = 0 then
            this.Emit("tick", "prebeat")

    member this.PostPhysics(_: single) : unit =
        post <- post + 1

        if post = 1 then
            this.Emit("tick", "postfirst")
        elif post % 30 = 0 then
            this.Emit("tick", "postbeat")

    member this.OnTickSimulate(_: single) : unit =
        let old = fixedCount
        fixedCount <- fixedCount + 1

        if old = 0 then
            this.Emit("tick", "updatefirst")
        elif fixedCount % 30 = 0 then
            this.Emit("tick", "updatebeat")
