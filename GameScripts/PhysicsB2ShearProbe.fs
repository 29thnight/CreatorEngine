namespace CreatorEngine.Scripts

open System
open CreatorEngine

type PhysicsB2ShearProbe() =
    inherit Component()

    let mutable elapsed = 0.0f
    let mutable requested = false

    member this.PostPhysics(dt: single) : unit =
        if not requested && Environment.GetEnvironmentVariable("CE_PHYSICS_B2_SHEAR") = "1" then
            elapsed <- elapsed + dt

            if elapsed >= 0.2f then
                requested <- true
                this.Entity.Parent.Transform.LocalScale <- Float3(2.0f, 1.0f, 1.0f)
                this.Transform.LocalRotation <- Quaternion(0.0f, 0.38268343f, 0.0f, 0.92387953f)
                Console.WriteLine("[physics.player.b2.shear] requested=true")
