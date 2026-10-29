namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type DaggerConvexPlayerProbe() =
    inherit Component()

    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable tick = 0
    let mutable passed = 0
    let mutable phase = 0
    let mutable elapsed = 0.0f
    let mutable launchTime = 0.0f
    let mutable impulseTime = 0.0f
    let mutable finished = false
    let mutable startX = 0.0f
    let mutable beforeImpulse = 0.0f
    let mutable startRotation = Unchecked.defaultof<Quaternion>

    member private _.Check(value, message) =
        if not value then invalidOp message
        passed <- passed + 1

    static member private Speed(value: Float3) =
        MathF.Sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z)

    member this.PostPhysics(delta: single) : unit =
        if not finished then
            try
                if obj.ReferenceEquals(body, null) then
                    body <- this.GetComponent<PhysicsBodyComponent>()
                let mutable state = Unchecked.defaultof<PhysicsBodyState>
                if obj.ReferenceEquals(body, null) || body.ReadState(&state) <> PhysicsError.None then
                    invalidOp "dynamic body lookup/read"
                tick <- tick + 1
                elapsed <- elapsed + delta
                let mutable sample = false
                if not (Single.IsFinite(state.Position.X) && Single.IsFinite(state.Position.Y)
                        && Single.IsFinite(state.Position.Z) && Single.IsFinite(DaggerConvexPlayerProbe.Speed(state.LinearVelocity))
                        && Single.IsFinite(DaggerConvexPlayerProbe.Speed(state.AngularVelocity))) then
                    invalidOp "nonfinite simulation state"
                if phase = 0 then
                    this.Check(state.Kind = PhysicsBodyKind.Dynamic, "dynamic kind")
                    this.Check(abs (state.Mass - 1.0f) < 0.001f, "one kilogram mass")
                    let mutable count = 0
                    let mutable shape = Unchecked.defaultof<PhysicsShapeState>
                    this.Check(body.GetShapeCount(&count) = PhysicsError.None && count = 1
                               && body.GetShape(0, &shape) = PhysicsError.None && shape.Kind = PhysicsShapeKind.Convex,
                               "single real convex")
                    phase <- 1
                    sample <- true
                if phase = 1 && elapsed >= 0.5f then
                    this.Check(state.Position.Y < 2.5f && state.LinearVelocity.Y < -1.0f, "gravity fall")
                    phase <- 2
                    sample <- true
                if phase = 2 && elapsed >= 4.0f then
                    this.Check(state.Position.Y > -0.05f && state.Position.Y < 0.4f, "floor contact prevents tunneling")
                    this.Check(DaggerConvexPlayerProbe.Speed(state.LinearVelocity) < 0.15f, "contact settling")
                    startX <- state.Position.X
                    startRotation <- state.Rotation
                    this.Check(body.SetVelocity(Float3(1.0f, 2.0f, 0.0f), Float3(0.0f, 0.0f, 2.0f)) = PhysicsError.None, "linear/angular command")
                    launchTime <- elapsed
                    phase <- 3
                    sample <- true
                if phase = 3 && elapsed >= launchTime + 0.2f then
                    this.Check(state.Position.X > startX + 0.1f, "linear movement")
                    this.Check(state.Position.Y > 0.15f, "vertical launch")
                    let rotationDot = abs (state.Rotation.X * startRotation.X + state.Rotation.Y * startRotation.Y
                                           + state.Rotation.Z * startRotation.Z + state.Rotation.W * startRotation.W)
                    this.Check(DaggerConvexPlayerProbe.Speed(state.AngularVelocity) > 0.5f && rotationDot < 0.999f, "angular movement and pose rotation")
                    beforeImpulse <- state.LinearVelocity.X
                    this.Check(body.ApplyForce(Float3(2.0f, 0.0f, 0.0f), mode = PhysicsForceMode.Impulse) = PhysicsError.None, "impulse submission")
                    impulseTime <- elapsed
                    phase <- 4
                    sample <- true
                if phase = 4 && elapsed >= impulseTime + 0.03f then
                    this.Check(state.LinearVelocity.X > beforeImpulse + 1.5f, "impulse velocity response")
                    phase <- 5
                    sample <- true
                if phase = 5 && elapsed >= 10.0f then
                    this.Check(state.Position.Y > -0.05f && state.Position.Y < 0.5f && DaggerConvexPlayerProbe.Speed(state.LinearVelocity) < 0.2f, "second landing/settling")
                    let visual = this.Transform.WorldPosition
                    let dx, dy, dz = visual.X - state.Position.X, visual.Y - state.Position.Y, visual.Z - state.Position.Z
                    this.Check(dx * dx + dy * dy + dz * dz < 1e-6f, "render entity follows physics pose")
                    finished <- true
                    sample <- true
                    Console.WriteLine("[physics.player.dagger] " + JsonSerializer.Serialize({| passed = passed; failed = 0; tick = tick; height = state.Position.Y; speed = DaggerConvexPlayerProbe.Speed(state.LinearVelocity); complete = true |}))
                if sample then
                    Console.WriteLine("[physics.player.dagger.sample] " + JsonSerializer.Serialize({| tick = tick; seconds = elapsed; x = state.Position.X; y = state.Position.Y; vx = state.LinearVelocity.X; vy = state.LinearVelocity.Y; angularSpeed = DaggerConvexPlayerProbe.Speed(state.AngularVelocity) |}))
            with error ->
                finished <- true
                Console.WriteLine("[physics.player.dagger] " + JsonSerializer.Serialize({| passed = passed; failed = 1; tick = tick; complete = false |}))
                Console.WriteLine("[physics.player.dagger.failure] " + error.Message)
