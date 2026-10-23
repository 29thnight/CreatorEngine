namespace CreatorEngine.Scripts

open System
open CreatorEngine

type FixedUpdateContractProbe() =
    inherit Component()

    static let mutable completed = 0
    static let mutable failures = 0
    static let mutable faultDisabled = 0
    static let mutable removedEnded = 0
    static let mutable ended = 0
    static let mutable driverUpdates = 0

    let mutable updates = 0
    let mutable disabledCount = 0
    let mutable inFrame = 0
    let mutable maxInFrame = 0
    let mutable doneOnce = false
    let mutable frame = 0UL
    member private _.Updates = updates

    static member private Require(value, reason) =
        if not value then
            failures <- failures + 1
            invalidOp reason

    [<EngineCallable>]
    static member State() =
        String.concat
            ""
            [
                $"{{\"completed\":{completed}"
                $",\"failures\":{failures}"
                $",\"faultDisabled\":{faultDisabled}"
                $",\"removedEnded\":{removedEnded}"
                $",\"ended\":{ended}"
                $",\"driverUpdates\":{driverUpdates}}}"
            ]

    member this.OnTickSimulate(dt: single) : unit =
        let require = FixedUpdateContractProbe.Require
        require (abs (dt - 1.0f / 60.0f) < 1e-7f, "OnTickSimulate did not receive fixedDelta")
        updates <- updates + 1

        if frame <> Component.FrameCount then
            frame <- Component.FrameCount
            inFrame <- 0

        inFrame <- inFrame + 1
        maxInFrame <- max maxInFrame inFrame

        match this.Entity.Name with
        | "FixedFault" ->
            if updates <> 1 then
                failures <- failures + 1

            invalidOp "[fixed-update.injected] isolate this component"
        | "FixedRemove" ->
            require (updates = 1, "destroyed instance updated twice")
            this.Entity.Destroy()
        | "FixedDriver" ->
            driverUpdates <- driverUpdates + 1
            let target = Entity.Find("FixedTarget").GetComponent<FixedUpdateContractProbe>()
            require (not (obj.ReferenceEquals(target, null)), "target missing")

            if updates = 6 then
                target.Enabled <- false
                disabledCount <- target.Updates

            if updates = 9 then
                require (target.Updates = disabledCount, "disabled component still updated")
                target.Enabled <- true

            if updates >= 30 && not doneOnce then
                require (target.Updates > disabledCount, "reenabled component did not update")
                require (faultDisabled = completed + 1, "fault not isolated exactly once")
                require (removedEnded = completed + 1, "removed component teardown missing")
                doneOnce <- true
                completed <- completed + 1

                Console.WriteLine(
                    $"[FIXED_UPDATE_PRODUCT] completed={completed} updates={updates} target={target.Updates} dt={dt:R} maxPerFrame={maxInFrame}"
                )
        | _ -> ()

    member this.OnDisable() : unit =
        if this.Entity.Name = "FixedFault" then
            faultDisabled <- faultDisabled + 1

    member this.OnEndSimulation() : unit =
        ended <- ended + 1

        if this.Entity.Name = "FixedRemove" then
            removedEnded <- removedEnded + 1
