namespace CreatorEngine.Scripts

open System
open System.Text.Json
open CreatorEngine

type PhysicsB2PlayerProbe() =
    inherit Component()
    let mutable body = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable phase = 0
    let mutable passed = 0
    let mutable elapsed = 0.0f
    let mutable changedAt = 0.0f
    let mutable expectedX = 0.0f
    let mutable finished = false
    let mutable dynamicQueries = Unchecked.defaultof<PhysicsB2DynamicQueryProbe>
    let mutable mixedQueries = Unchecked.defaultof<PhysicsB2MixedQueryProbe>
    let mutable benchmark = Unchecked.defaultof<PhysicsB2QueryBenchmark>

    member private _.Check(value, message) =
        if not value then invalidOp message
        passed <- passed + 1

    member private this.CheckPose(state: PhysicsBodyState) =
        let position = this.Transform.WorldPosition
        let x, y, z = position.X - state.Position.X, position.Y - state.Position.Y, position.Z - state.Position.Z
        this.Check(x * x + y * y + z * z < 1e-5f, "authoritative Transform matches completed body under rotated/scaled parent")

    member this.PostPhysics(delta: single) =
        if finished then
            dynamicQueries.Run(delta)
            mixedQueries.Run(delta)
            benchmark.Run(body)
        else
            try
                elapsed <- elapsed + delta
                if obj.ReferenceEquals(body, null) then
                    body <- this.GetComponent<PhysicsBodyComponent>()
                    dynamicQueries <- PhysicsB2DynamicQueryProbe(this.Entity)
                    mixedQueries <- PhysicsB2MixedQueryProbe(this.Entity)
                    benchmark <- PhysicsB2QueryBenchmark(this.Entity)
                let mutable state = Unchecked.defaultof<PhysicsBodyState>
                if obj.ReferenceEquals(body, null) || body.ReadState(&state) <> PhysicsError.None then invalidOp "body lookup/read"
                let dynamic = this.Entity.Name = "B2Dynamic"
                let kinematic = this.Entity.Name = "B2Kinematic"
                if phase = 0 then
                    let kind = if dynamic then PhysicsBodyKind.Dynamic elif kinematic then PhysicsBodyKind.Kinematic else PhysicsBodyKind.Static
                    this.Check(state.Kind = kind, "body kind")
                    phase <- 1
                if phase = 1 && elapsed >= 0.2f then
                    expectedX <- if dynamic then 4.0f elif kinematic then 30.0f else 20.0f
                    this.Transform.WorldPosition <- Float3(expectedX, 5.0f, 0.0f)
                    changedAt <- elapsed
                    phase <- 2
                elif phase = 2 && elapsed >= changedAt + 0.1f then
                    this.Check(state.Position.X >= expectedX - 0.01f && state.Position.X < expectedX + (if dynamic then 1.5f else 0.01f), "authored world pose reaches SDK")
                    this.CheckPose(state)
                    expectedX <- state.Position.X + 2.0f
                    let parent = this.Entity.Parent.Transform
                    parent.LocalPosition <- parent.LocalPosition + Float3(2.0f, 0.0f, 0.0f)
                    changedAt <- elapsed
                    phase <- 3
                elif phase = 3 && elapsed >= changedAt + 0.1f then
                    this.Check(state.Position.X >= expectedX - 0.01f && state.Position.X < expectedX + (if dynamic then 1.5f else 0.01f), "parent translation reaches SDK")
                    this.CheckPose(state)
                    this.Entity.Parent.Transform.LocalScale <- Float3(3.0f, 3.0f, 3.0f)
                    expectedX <- this.Transform.WorldPosition.X
                    changedAt <- elapsed
                    phase <- 4
                elif phase = 4 && elapsed >= changedAt + 0.1f then
                    this.Check(state.Position.X >= expectedX - 0.01f && state.Position.X < expectedX + (if dynamic then 1.5f else 0.01f), "parent scale preserves world pose policy")
                    this.CheckPose(state)
                    let hits = Array.zeroCreate<PhysicsHit> 8
                    let offset = this.Transform.Right * (if dynamic then 1.5f else 1.4f)
                    let query = Physics.OverlapSphere(this.Entity, state.Position + offset, 0.05f, Span(hits))
                    let mutable own = false
                    for i in 0 .. query.Written - 1 do own <- own || hits[i].Entity = this.Entity
                    this.Check(query.Succeeded && own, "parent scale updates actual collision geometry")
                    if dynamic then this.Check(body.SetVelocity(Float3.Zero) = PhysicsError.None, "halt dynamic fixture")
                    else this.Check(state.Kind = (if kinematic then PhysicsBodyKind.Kinematic else PhysicsBodyKind.Static), "motion kind preserved during shape replacement")
                    PhysicsB2BatchChecks.Run(this.Entity, state, dynamic)
                    PhysicsB2BatchChecks.Dense(this.Entity, state, dynamic)
                    if dynamic && Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_STRESS") = "1" then PhysicsB2BatchChecks.Stress(this.Entity)
                    finished <- true
                    Console.WriteLine("[physics.player.b2] " + JsonSerializer.Serialize({| role = this.Entity.Name; passed = passed; failed = 0; seconds = elapsed; x = state.Position.X; complete = true |}))
            with error ->
                finished <- true
                Console.WriteLine("[physics.player.b2] " + JsonSerializer.Serialize({| role = this.Entity.Name; passed = passed; failed = 1; complete = false |}))
                Console.WriteLine($"[physics.player.b2.failure] {this.Entity.Name}: {error.Message}")
