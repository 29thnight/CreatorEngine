namespace CreatorEngine.Scripts

open System
open System.Collections.Generic
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine

type internal PhysicsB2BatchChecks private () =
    static member Run(owner: Entity, state: PhysicsBodyState, dynamic: bool) =
        let mutable passed = 0
        let check value message =
            if not value then invalidOp ("batch: " + message)
            passed <- passed + 1
        let requests = Array.zeroCreate<PhysicsQueryRequest> 4
        let hits = Array.zeroCreate<PhysicsHit> 16
        let results = Array.zeroCreate<PhysicsBatchResult> 4
        let point = state.Position + owner.Transform.Right * (if dynamic then 1.5f else 1.4f)
        let down = Float3(0.0f, -1.0f, 0.0f)
        requests[0] <- PhysicsQueryRequest.OverlapSphere(point, 0.05f, 0, 8)
        requests[1] <- PhysicsQueryRequest.Raycast(state.Position, Float3.Zero, 10.0f, 8, 0)
        requests[2] <- PhysicsQueryRequest.OverlapSphere(point, 0.05f, 8, 0)
        requests[3] <- PhysicsQueryRequest.Raycast(state.Position + Float3(1000000.0f, 20.0f, 0.0f), down, 40.0f, 8, 8)
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results)) = PhysicsError.None) "mixed read window"
        let mutable own = false
        for i in 0 .. results[0].Written - 1 do own <- own || hits[i].Entity = owner
        check (results[0].Succeeded && own) "overlap entity ownership"
        check (results[1].Error = PhysicsError.InvalidArgument && results[1].Written = 0) "individual error"
        check (results[2].Succeeded && results[2].RequiredCapacity > 0 && results[2].Truncated) "capacity discovery"
        check (results[3].Succeeded && results[3].Written = 0) "no-hit ray after invalid request"
        let before = results[0]
        requests[3] <- PhysicsQueryRequest.Raycast(state.Position, down, 40.0f, 0, 8)
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results)) = PhysicsError.InvalidArgument) "overlap ranges rejected"
        check (results[0].Written = before.Written && results[0].Error = before.Error) "batch error preserves outputs"
        requests[3] <- PhysicsQueryRequest.Raycast(state.Position, down, 40.0f, 8, 8)
        let aliased = MemoryMarshal.Cast<PhysicsHit, PhysicsBatchResult>(Span(hits)).Slice(0, 4)
        let savedComponent = hits[0].ComponentId
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), aliased) = PhysicsError.InvalidArgument) "cross-buffer alias rejected"
        check (hits[0].ComponentId = savedComponent) "alias failure preserves hits"
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results, 0, 1)) = PhysicsError.InvalidArgument) "summary length mismatch"
        check (Physics.QueryBatch(owner, ReadOnlySpan<PhysicsQueryRequest>.Empty, Span<PhysicsHit>.Empty, Span<PhysicsBatchResult>.Empty) = PhysicsError.None) "empty owner batch"
        requests[3] <- PhysicsQueryRequest.Raycast(state.Position, down, 40.0f, -1, 1)
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results)) = PhysicsError.InvalidArgument) "negative offset rejected"
        requests[3] <- PhysicsQueryRequest.Raycast(state.Position, down, 40.0f, Int32.MaxValue, 1)
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results)) = PhysicsError.InvalidArgument) "offset overflow rejected"
        let maximum = Array.init Physics.MaxBatchRequests (fun _ -> PhysicsQueryRequest.Raycast(state.Position + Float3(1000000.0f, 20.0f, 0.0f), down, 40.0f, 0, 0))
        let maximumResults = Array.zeroCreate<PhysicsBatchResult> Physics.MaxBatchRequests
        check (Physics.QueryBatch(owner, ReadOnlySpan(maximum), Span<PhysicsHit>.Empty, Span(maximumResults)) = PhysicsError.None) "maximum batch accepted"
        check (maximumResults |> Array.forall (fun result -> result.Succeeded && result.Written = 0 && result.RequiredCapacity = 0)) "all maximum batch slots written"
        check (Physics.QueryBatch(owner, ReadOnlySpan(Array.zeroCreate<PhysicsQueryRequest> (Physics.MaxBatchRequests + 1)), Span<PhysicsHit>.Empty,
                  Span(Array.zeroCreate<PhysicsBatchResult> (Physics.MaxBatchRequests + 1))) = PhysicsError.InvalidArgument) "maximum request overflow rejected"
        Console.WriteLine("[physics.player.batch] " + JsonSerializer.Serialize({| role = owner.Name; passed = passed; failed = 0; complete = true |}))

    static member Dense(owner: Entity, state: PhysicsBodyState, dynamic: bool) =
        let mutable passed = 0
        let check value message =
            if not value then invalidOp ("dense batch: " + message)
            passed <- passed + 1
        let hits = Array.zeroCreate<PhysicsHit> Physics.MaxBatchHitCapacity
        MemoryMarshal.AsBytes(Span(hits)).Fill(0x5auy)
        let requests = Array.zeroCreate<PhysicsQueryRequest> 5
        let results = Array.zeroCreate<PhysicsBatchResult> 5
        let point = state.Position + owner.Transform.Right * (if dynamic then 1.5f else 1.4f)
        requests[0] <- PhysicsQueryRequest.OverlapSphere(point, 0.05f, 4000, 8)
        requests[1] <- PhysicsQueryRequest.Raycast(state.Position, Float3.Zero, 10.0f, 32, 8)
        requests[2] <- PhysicsQueryRequest.OverlapSphere(point, 0.05f, 0, 1)
        requests[3] <- PhysicsQueryRequest.Raycast(state.Position + Float3(1000000.0f, 20.0f, 0.0f), Float3(0.0f, -1.0f, 0.0f), 40.0f, 1024, 8)
        requests[4] <- PhysicsQueryRequest.OverlapSphere(point, 0.05f, 64, 0)
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results)) = PhysicsError.None) "sparse reverse offsets"
        let mutable own = false
        for i in 0 .. results[0].Written - 1 do own <- own || hits[4000 + i].Entity = owner
        check (results[0].Succeeded && own) "high output offset identity"
        check (results[2].Succeeded && results[2].Written = 1 && hits[0].Entity = owner) "request order differs from output order"
        check (results[1].Error = PhysicsError.InvalidArgument && results[1].Written = 0) "individual error between successful requests"
        check (results[3].Succeeded && results[3].Written = 0 && results[4].Succeeded && results[4].Written = 0 && results[4].RequiredCapacity > 0 && results[4].Truncated) "no-hit and capacity discovery"
        let mutable preserved = true
        for i in 0 .. hits.Length - 1 do
            if not (i < results[2].Written || (i >= 4000 && i < 4000 + results[0].Written)) then
                let bytes = MemoryMarshal.AsBytes(Span(hits, i, 1))
                for j in 0 .. bytes.Length - 1 do preserved <- preserved && bytes[j] = 0x5auy
        check preserved "all gaps, unwritten tails and failed request bytes preserved"
        Console.WriteLine("[physics.player.dense-batch] " + JsonSerializer.Serialize({| role = owner.Name; passed = passed; failed = 0; complete = true |}))

    static member Stress(owner: Entity) =
        let mutable passed = 0
        let check value message =
            if not value then invalidOp ("stress: " + message)
            passed <- passed + 1
        let point = Float3(100.0f, 0.0f, 0.0f)
        let full = Array.zeroCreate<PhysicsHit> 256
        let scalar = Physics.OverlapSphere(owner, point, 4.0f, Span(full))
        check (scalar.Succeeded && scalar.Written = 128 && scalar.RequiredCapacity = 128 && not scalar.Truncated) "full density count"
        let ids = HashSet<uint64>()
        for i in 0 .. scalar.Written - 1 do
            if not (full[i].Entity.Name.StartsWith("DenseQuery", StringComparison.Ordinal)) then invalidOp "stress ownership"
            ids.Add(full[i].ComponentId) |> ignore
        check (ids.Count = 128) "distinct body identity"
        let hits = Array.zeroCreate<PhysicsHit> Physics.MaxBatchHitCapacity
        MemoryMarshal.AsBytes(Span(hits)).Fill(0x5auy)
        let requests = [|
            PhysicsQueryRequest.OverlapSphere(point, 4.0f, 1024, 256)
            PhysicsQueryRequest.OverlapSphere(point, 4.0f, 0, 8)
            PhysicsQueryRequest.OverlapSphere(point, 4.0f, 32, 0)
            PhysicsQueryRequest.Raycast(point, Float3.Zero, 10.0f, 4000, 8)
            PhysicsQueryRequest.Raycast(Float3(1000000.0f, 20.0f, 0.0f), Float3(0.0f, -1.0f, 0.0f), 40.0f, 2048, 8) |]
        let results = Array.zeroCreate<PhysicsBatchResult> 5
        check (Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(hits), Span(results)) = PhysicsError.None) "mixed density batch"
        check (results[0].Succeeded && results[0].Written = 128 && results[0].RequiredCapacity = 128 && not results[0].Truncated) "full batch count"
        check (results[1].Written = 8 && results[1].RequiredCapacity = 128 && results[1].Truncated && results[2].Written = 0 && results[2].RequiredCapacity = 128 && results[2].Truncated) "overflow and discovery"
        check (results[3].Error = PhysicsError.InvalidArgument && results[4].Succeeded && results[4].Written = 0) "error and no-hit after overflow"
        let batchIds = HashSet<uint64>()
        for i in 0 .. 127 do batchIds.Add(hits[1024 + i].ComponentId) |> ignore
        let mutable preserved = batchIds.SetEquals(ids)
        for i in 0 .. 7 do preserved <- preserved && ids.Contains(hits[i].ComponentId)
        for i in 0 .. hits.Length - 1 do
            if not (i < 8 || (i >= 1024 && i < 1152)) then
                let bytes = MemoryMarshal.AsBytes(Span(hits, i, 1))
                for j in 0 .. bytes.Length - 1 do preserved <- preserved && bytes[j] = 0x5auy
        check preserved "identity and all untouched bytes"
        Console.WriteLine("[physics.player.stress] " + JsonSerializer.Serialize({| passed = passed; failed = 0; bodies = 128; capacity = 4096; written = 136; complete = true |}))
        let mutable maximumPassed = 0
        let maximumCheck value message =
            if not value then invalidOp ("maximum batch: " + message)
            maximumPassed <- maximumPassed + 1
        let backing = Array.zeroCreate<PhysicsHit> (Physics.MaxBatchHitCapacity + 2)
        MemoryMarshal.AsBytes(Span(backing)).Fill(0x5auy)
        let output = Span(backing, 1, Physics.MaxBatchHitCapacity)
        let maximum = Array.init 64 (fun i -> PhysicsQueryRequest.OverlapSphere(point, 4.0f, (63 - i) * 64, 64))
        let summaries = Array.zeroCreate<PhysicsBatchResult> 64
        maximumCheck (Physics.QueryBatch(owner, ReadOnlySpan(maximum), output, Span(summaries)) = PhysicsError.None) "maximum request and hit capacity"
        let mutable written = 0
        let mutable counts = true
        let mutable identities = true
        for i in 0 .. 63 do
            counts <- counts && summaries[i].Succeeded && summaries[i].Written = 64 && summaries[i].RequiredCapacity = 128 && summaries[i].Truncated
            written <- written + summaries[i].Written
            let requestIds = HashSet<uint64>()
            for j in 0 .. 63 do
                let hit = output[(63 - i) * 64 + j]
                identities <- identities && ids.Contains(hit.ComponentId) && hit.Entity.Name.StartsWith("DenseQuery", StringComparison.Ordinal)
                requestIds.Add(hit.ComponentId) |> ignore
            identities <- identities && requestIds.Count = 64
        maximumCheck (counts && written = 4096) "every request count and truncation"
        maximumCheck identities "every output identity and per-request uniqueness"
        let mutable guards = true
        let firstGuard = MemoryMarshal.AsBytes(Span(backing, 0, 1))
        let lastGuard = MemoryMarshal.AsBytes(Span(backing, backing.Length - 1, 1))
        for i in 0 .. firstGuard.Length - 1 do guards <- guards && firstGuard[i] = 0x5auy
        for i in 0 .. lastGuard.Length - 1 do guards <- guards && lastGuard[i] = 0x5auy
        maximumCheck guards "both output boundary guards preserved"
        Console.WriteLine("[physics.player.max-batch] " + JsonSerializer.Serialize({| passed = maximumPassed; failed = 0; requests = 64; capacity = 4096; written = written; guardBytes = 128; complete = true |}))
