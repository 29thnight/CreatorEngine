namespace CreatorEngine.Scripts

open System
open CreatorEngine

type PhysicsProbe() =
    inherit Component()

    let mutable checkedOnce = false

    member private _.Check(name, valid) =
        if valid then
            Component.Log($"[PhysicsProbe] PASS {name}")
        else
            Component.LogError($"[PhysicsProbe] FAIL {name}")

    member this.PostPhysics(_: single) : unit =
        if not checkedOnce then
            checkedOnce <- true
            let origin = this.Transform.WorldPosition
            let values = Array.zeroCreate<PhysicsHit> 32
            let hits = Span<PhysicsHit>(values)
            let overlap = Physics.OverlapSphere(this.Entity, origin, 50.0f, hits)
            this.Check("overlap succeeds", overlap.Succeeded)
            this.Check("fixture has shapes", overlap.RequiredCapacity > 0)
            this.Check("written count bounded", overlap.Written <= hits.Length)

            let discovery =
                Physics.OverlapSphere(this.Entity, origin, 50.0f, Span<PhysicsHit>.Empty)

            this.Check(
                "capacity discovery",
                discovery.Succeeded
                && discovery.Written = 0
                && discovery.RequiredCapacity = overlap.RequiredCapacity
                && discovery.Truncated = (discovery.RequiredCapacity > 0)
            )

            let tiny = Span<PhysicsHit>(values, 0, 1)
            let small = Physics.OverlapSphere(this.Entity, origin, 50.0f, tiny)

            this.Check(
                "overflow contract",
                small.Succeeded
                && small.RequiredCapacity = overlap.RequiredCapacity
                && small.Written <= tiny.Length
                && small.Truncated = (small.RequiredCapacity > tiny.Length)
            )

            let ray =
                Physics.Raycast(this.Entity, origin + Float3(0.0f, 5.0f, 0.0f), Float3(0.0f, -1.0f, 0.0f), 100.0f, hits)

            this.Check("ray succeeds", ray.Succeeded)

            for i in 0 .. ray.Written - 1 do
                let hit = hits[i]

                this.Check(
                    "hit identity and location",
                    hit.Entity.IsAlive
                    && hit.ComponentId <> 0UL
                    && hit.ShapeId <> 0u
                    && hit.HasLocation
                    && hit.Distance >= 0.0f
                    && hit.Distance <= 100.0f
                )

            let invalid = Physics.OverlapSphere(this.Entity, origin, -1.0f, hits)

            this.Check(
                "invalid query reports error",
                invalid.Error = PhysicsError.InvalidArgument && invalid.Written = 0
            )
