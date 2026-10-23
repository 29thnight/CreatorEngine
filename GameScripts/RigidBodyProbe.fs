namespace CreatorEngine.Scripts

open CreatorEngine

type RigidBodyProbe() =
    inherit Component()

    let mutable checkedOnce = false

    member private _.Check(name, valid) =
        if valid then
            Component.Log($"[RigidBodyProbe] PASS {name}")
        else
            Component.LogError($"[RigidBodyProbe] FAIL {name}")

    member this.PostPhysics(_: single) : unit =
        if not checkedOnce then
            checkedOnce <- true
            let body = this.GetComponent<PhysicsBodyComponent>()

            if obj.ReferenceEquals(body, null) then
                Component.LogError("[RigidBodyProbe] FAIL required body missing or ambiguous")
            else
                let mutable before = Unchecked.defaultof<PhysicsBodyState>
                this.Check("state", body.ReadState(&before) = PhysicsError.None)
                let mutable count = 0
                this.Check("shape count", body.GetShapeCount(&count) = PhysicsError.None && count > 0)

                for i in 0 .. count - 1 do
                    let mutable shape = Unchecked.defaultof<PhysicsShapeState>

                    this.Check(
                        "stable shape identity",
                        body.GetShape(i, &shape) = PhysicsError.None && shape.ShapeId <> 0u
                    )

                let same = PhysicsBodyComponent.Find(this.Entity, body.ComponentId)

                this.Check(
                    "explicit identity lookup",
                    not (obj.ReferenceEquals(same, null)) && same.ComponentId = body.ComponentId
                )

                if before.Kind = PhysicsBodyKind.Dynamic then
                    let velocity = Float3(1.0f, 2.0f, 3.0f)
                    this.Check("velocity update", body.SetVelocity(velocity) = PhysicsError.None)
                    let mutable after = Unchecked.defaultof<PhysicsBodyState>

                    this.Check(
                        "velocity round trip",
                        body.ReadState(&after) = PhysicsError.None
                        && abs (after.LinearVelocity.X - velocity.X) < 0.001f
                        && abs (after.LinearVelocity.Y - velocity.Y) < 0.001f
                        && abs (after.LinearVelocity.Z - velocity.Z) < 0.001f
                    )

                    this.Check(
                        "restore velocities",
                        body.SetVelocity(before.LinearVelocity, before.AngularVelocity) = PhysicsError.None
                    )
