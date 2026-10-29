namespace CreatorEngine.Scripts

open System
open System.IO
open System.Text.Json
open CreatorEngine

type internal PhysicsB2MixedQueryProbe(owner: Entity) =
    let mutable phase = 0
    let mutable passed = 0
    let mutable time = 0.0f
    let mutable dynamicBody = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable kinematicBody = Unchecked.defaultof<PhysicsBodyComponent>
    let mutable kinematicOwner = Unchecked.defaultof<Entity>

    member private _.Check(value, message) =
        if not value then invalidOp message
        passed <- passed + 1

    member private this.Query(point: Float3, expected: uint64, present: bool) =
        let scalar = Array.zeroCreate<PhysicsHit> 8
        let batch = Array.zeroCreate<PhysicsHit> 8
        let requests = [| PhysicsQueryRequest.OverlapSphere(point, 0.1f, 0, 8) |]
        let results = Array.zeroCreate<PhysicsBatchResult> 1
        let answer = Physics.OverlapSphere(owner, point, 0.1f, Span(scalar))
        this.Check(Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(batch), Span(results)) = PhysicsError.None
                   && answer.Succeeded && results[0].Succeeded && not answer.Truncated && not results[0].Truncated
                   && answer.Written = results[0].Written, "mixed scalar/batch counts")
        let mutable found = false
        let mutable parity = true
        for i in 0 .. answer.Written - 1 do
            found <- found || scalar[i].ComponentId = expected
            let mutable matched = false
            for j in 0 .. results[0].Written - 1 do
                matched <- matched || (scalar[i].ComponentId = batch[j].ComponentId && scalar[i].Entity = batch[j].Entity && scalar[i].ShapeId = batch[j].ShapeId)
            parity <- parity && matched
        this.Check(parity && found = present, "mixed query identity/presence")

    member this.Run(delta: single) =
        let gate = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_GATE")
        if owner.Name = "B2Dynamic" && phase <> 4 && Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_MIXED") = "1"
           && (String.IsNullOrEmpty(gate) || File.Exists(gate)) then
            try
                time <- time + delta
                if time > 8.0f then invalidOp "mixed query timeout"
                if phase = 0 then
                    dynamicBody <- Entity.Find("MixedDynamic").GetComponent<PhysicsBodyComponent>()
                    kinematicOwner <- Entity.Find("MixedKinematic")
                    kinematicBody <- kinematicOwner.GetComponent<PhysicsBodyComponent>()
                    this.Check(not (obj.ReferenceEquals(dynamicBody, null)) && not (obj.ReferenceEquals(kinematicBody, null)), "mixed body lookup")
                    this.Query(Float3(110.0f, 20.0f, 1.5f), kinematicBody.ComponentId, false)
                    kinematicOwner.Transform.WorldRotation <- Quaternion(0.0f, 0.70710678f, 0.0f, 0.70710678f)
                    this.Check(dynamicBody.SetVelocity(Float3(10.0f, 0.0f, 0.0f)) = PhysicsError.None, "collision velocity")
                    phase <- 1
                else
                    let mutable moving = Unchecked.defaultof<PhysicsBodyState>
                    let mutable target = Unchecked.defaultof<PhysicsBodyState>
                    this.Check(dynamicBody.ReadState(&moving) = PhysicsError.None && kinematicBody.ReadState(&target) = PhysicsError.None, "mixed completed states")
                    if phase = 1 && time > 0.15f then
                        this.Query(Float3(110.0f, 20.0f, 1.5f), kinematicBody.ComponentId, true)
                        this.Query(Float3(111.5f, 20.0f, 0.0f), kinematicBody.ComponentId, false)
                        kinematicOwner.Transform.WorldPosition <- Float3(115.0f, 20.0f, 0.0f)
                        phase <- 2
                    elif phase = 2 && time > 0.35f then
                        this.Query(Float3(110.0f, 20.0f, 1.5f), kinematicBody.ComponentId, false)
                        this.Query(Float3(115.0f, 20.0f, 1.5f), kinematicBody.ComponentId, true)
                        phase <- 3
                    elif phase = 3 && time > 1.0f then
                        this.Check(moving.Position.X > 103.0f && moving.Position.X < 104.2f && abs moving.LinearVelocity.X < 0.1f, "dynamic collision stops at static wall")
                        this.Query(moving.Position, dynamicBody.ComponentId, true)
                        this.Query(Float3(100.0f, 20.0f, 0.0f), dynamicBody.ComponentId, false)
                        phase <- 4
                        Console.WriteLine("[physics.player.mixed-query] " + JsonSerializer.Serialize({| passed = passed; failed = 0; collisionX = moving.Position.X; complete = true |}))
            with error ->
                phase <- 4
                Console.WriteLine("[physics.player.mixed-query.failure] " + error.Message)
