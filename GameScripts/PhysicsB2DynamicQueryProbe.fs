namespace CreatorEngine.Scripts

open System
open System.IO
open System.Collections.Generic
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine

type internal PhysicsB2DynamicQueryProbe(owner: Entity) =
    let mutable bodies = Array.empty<PhysicsBodyComponent>
    let mutable initial = Array.empty<Float3>
    let ids = HashSet<uint64>()
    let mutable phase = 0
    let mutable passed = 0
    let mutable reads = 0
    let mutable stages = 0
    let mutable seconds = 0.0f
    let mutable previousDisplacement = 0.0f
    let mutable poseChanges = 0
    let mutable farCenter = Unchecked.defaultof<Float3>
    let mutable finished = false

    member private _.Check(value, message) =
        if not value then invalidOp message
        passed <- passed + 1

    member private _.SetVelocity(velocity: single) =
        for body in bodies do
            if body.SetVelocity(Float3(velocity, 0.0f, 0.0f)) <> PhysicsError.None then invalidOp "dynamic velocity command"

    member private _.Count(point: Float3) =
        let hits = Array.zeroCreate<PhysicsHit> 256
        let result = Physics.OverlapSphere(owner, point, 4.0f, Span(hits))
        if not result.Succeeded || result.Truncated || result.Written <> result.RequiredCapacity then invalidOp "dynamic scalar count"
        result.Written

    member private this.ValidateHits(point: Float3) =
        let hits = Array.zeroCreate<PhysicsHit> 256
        let result = Physics.OverlapSphere(owner, point, 4.0f, Span(hits))
        let found = HashSet<uint64>()
        for i in 0 .. result.Written - 1 do found.Add(hits[i].ComponentId) |> ignore
        this.Check(result.Succeeded && result.Written = 128 && result.RequiredCapacity = 128 && not result.Truncated && found.SetEquals(ids), "moving scalar query retains all identities")

    member private this.ValidateStage(point: Float3) =
        let backing = Array.zeroCreate<PhysicsHit> 4098
        MemoryMarshal.AsBytes(Span(backing)).Fill(0x5auy)
        let hits = Span(backing, 1, 4096)
        let requests = Array.zeroCreate<PhysicsQueryRequest> 32
        let results = Array.zeroCreate<PhysicsBatchResult> 32
        for i in 0 .. 31 do requests[i] <- PhysicsQueryRequest.OverlapSphere(point, 4.0f, (31 - i) * 128, 128)
        this.Check(Physics.QueryBatch(owner, ReadOnlySpan(requests), hits, Span(results)) = PhysicsError.None, "moving full-output batch")
        let mutable correct = true
        for i in 0 .. 31 do
            correct <- correct && results[i].Succeeded && results[i].Written = 128 && results[i].RequiredCapacity = 128 && not results[i].Truncated
            let found = HashSet<uint64>()
            for j in 0 .. 127 do found.Add(hits[(31 - i) * 128 + j].ComponentId) |> ignore
            correct <- correct && found.SetEquals(ids)
        this.Check(correct, "all 4096 moving output identities and counts")
        let mutable guarded = true
        let firstGuard = MemoryMarshal.AsBytes(Span(backing, 0, 1))
        let lastGuard = MemoryMarshal.AsBytes(Span(backing, 4097, 1))
        for i in 0 .. firstGuard.Length - 1 do guarded <- guarded && firstGuard[i] = 0x5auy
        for i in 0 .. lastGuard.Length - 1 do guarded <- guarded && lastGuard[i] = 0x5auy
        this.Check(guarded, "moving full-output boundary guards")
        for i in 0 .. 15 do
            requests[i] <-
                if i % 2 = 0 then PhysicsQueryRequest.Raycast(point + Float3(0.0f, 5.0f, 0.0f), Float3(0.0f, -1.0f, 0.0f), 10.0f, i * 256, 256)
                else PhysicsQueryRequest.OverlapSphere(point, 4.0f, i * 256, 256)
        this.Check(Physics.QueryBatch(owner, ReadOnlySpan(requests, 0, 16), hits, Span(results, 0, 16)) = PhysicsError.None, "moving mixed batch")
        for i in 0 .. 15 do
            let scalar = Array.zeroCreate<PhysicsHit> 256
            let answer =
                if i % 2 = 0 then Physics.Raycast(owner, point + Float3(0.0f, 5.0f, 0.0f), Float3(0.0f, -1.0f, 0.0f), 10.0f, Span(scalar))
                else Physics.OverlapSphere(owner, point, 4.0f, Span(scalar))
            let scalarIds = HashSet<uint64>()
            for j in 0 .. answer.Written - 1 do scalarIds.Add(scalar[j].ComponentId) |> ignore
            let groupedIds = HashSet<uint64>()
            for j in 0 .. results[i].Written - 1 do groupedIds.Add(hits[i * 256 + j].ComponentId) |> ignore
            let mutable metadata = true
            for j in 0 .. answer.Written - 1 do
                let mutable matched = false
                for k in 0 .. results[i].Written - 1 do
                    let other = hits[i * 256 + k]
                    matched <- matched || (scalar[j].Entity = other.Entity && scalar[j].ComponentId = other.ComponentId
                                && scalar[j].ShapeId = other.ShapeId && scalar[j].LayerId = other.LayerId
                                && scalar[j].HasLocation = other.HasLocation && abs (scalar[j].Distance - other.Distance) < 0.001f)
                metadata <- metadata && matched
            this.Check(answer.Succeeded && results[i].Succeeded && answer.Written = (if i % 2 = 0 then 4 else 128)
                       && answer.Written = results[i].Written && answer.RequiredCapacity = results[i].RequiredCapacity
                       && not answer.Truncated && not results[i].Truncated && scalarIds.Count = answer.Written && metadata && scalarIds.SetEquals(groupedIds),
                       "moving ray/overlap scalar-batch parity")
        stages <- stages + 1

    member this.Run(delta: single) =
        let gate = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_GATE")
        if owner.Name = "B2Dynamic" && not finished && Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_DYNAMIC") = "1"
           && (String.IsNullOrEmpty(gate) || File.Exists(gate)) then
            try
                seconds <- seconds + delta
                if seconds > 5.0f then invalidOp "dynamic query motion timed out"
                if bodies.Length = 0 then
                    bodies <- Array.zeroCreate 128
                    initial <- Array.zeroCreate 128
                    for i in 0 .. 127 do
                        let body = Entity.Find(sprintf "DenseQuery%03d" i).GetComponent<PhysicsBodyComponent>()
                        let mutable state = Unchecked.defaultof<PhysicsBodyState>
                        if obj.ReferenceEquals(body, null) || body.ReadState(&state) <> PhysicsError.None || state.Kind <> PhysicsBodyKind.Dynamic then
                            invalidOp "dynamic body lookup/kind"
                        bodies[i] <- body
                        initial[i] <- state.Position
                        ids.Add(body.ComponentId) |> ignore
                    this.Check(ids.Count = 128, "128 distinct dynamic body identities")
                    this.ValidateStage(initial[0])
                    this.SetVelocity(20.0f)
                    phase <- 1
                else
                    let mutable anchor = Unchecked.defaultof<PhysicsBodyState>
                    if bodies[0].ReadState(&anchor) <> PhysicsError.None then invalidOp "moving anchor read"
                    let displacement = anchor.Position.X - initial[0].X
                    if abs (displacement - previousDisplacement) > 0.0001f then poseChanges <- poseChanges + 1
                    previousDisplacement <- displacement
                    let mutable coherent = true
                    for i in 0 .. 127 do
                        let mutable state = Unchecked.defaultof<PhysicsBodyState>
                        if bodies[i].ReadState(&state) <> PhysicsError.None then invalidOp "moving body read"
                        coherent <- coherent && state.Kind = PhysicsBodyKind.Dynamic && abs (state.Position.X - initial[i].X - displacement) < 0.002f
                                    && abs (state.Position.Y - initial[i].Y) < 0.002f && abs (state.Position.Z - initial[i].Z) < 0.002f
                        if phase = 3 then coherent <- coherent && abs state.LinearVelocity.X < 0.001f
                    this.Check(coherent, "coherent dynamic motion and gravity-disabled pose")
                    this.ValidateHits(anchor.Position)
                    reads <- reads + 1
                    if phase = 1 && displacement >= 12.0f then
                        farCenter <- anchor.Position
                        this.Check(this.Count(initial[0]) = 0, "departed query volume is empty")
                        this.ValidateStage(anchor.Position)
                        this.SetVelocity(-20.0f)
                        phase <- 2
                    elif phase = 2 && displacement <= 0.02f then
                        this.Check(this.Count(initial[0]) = 128 && this.Count(farCenter) = 0, "return updates both query volumes")
                        this.ValidateStage(anchor.Position)
                        this.SetVelocity(0.0f)
                        phase <- 3
                    elif phase = 3 then
                        this.Check(reads >= 2 && poseChanges >= 2 && stages = 3, "all motion stages completed")
                        finished <- true
                        Console.WriteLine("[physics.player.dynamic-query] " + JsonSerializer.Serialize({| passed = passed; failed = 0; bodies = 128;
                            readWindows = reads; observedPoseChanges = poseChanges; stages = 3; writtenPerFullBatch = 4096;
                            departedHits = 0; returnedHits = 128; stoppedBodies = 128; complete = true |}))
            with error ->
                finished <- true
                Console.WriteLine("[physics.player.dynamic-query.failure] " + error.Message)
