namespace CreatorEngine.Scripts

open System
open System.IO
open System.Diagnostics
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine

module private QueryCpu =
    [<DllImport("kernel32.dll")>]
    extern nativeint GetCurrentThread()
    [<DllImport("kernel32.dll")>]
    extern uint32 GetCurrentThreadId()
    [<DllImport("kernel32.dll", SetLastError = true)>]
    extern bool GetThreadTimes(nativeint thread, uint64& creation, uint64& exitTime, uint64& kernel, uint64& user)
    [<DllImport("kernel32.dll", SetLastError = true)>]
    extern bool QueryThreadCycleTime(nativeint thread, uint64& cycles)

    let read() =
        let thread = GetCurrentThread()
        let mutable creation = 0UL
        let mutable exitTime = 0UL
        let mutable kernel = 0UL
        let mutable user = 0UL
        let mutable cycles = 0UL
        if not (GetThreadTimes(thread, &creation, &exitTime, &kernel, &user) && QueryThreadCycleTime(thread, &cycles)) then
            raise (System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()))
        struct (kernel + user, cycles, GetCurrentThreadId())

type internal PhysicsB2QueryBenchmark(owner: Entity) =
    let mutable block = 0
    let mutable failed = false

    member _.Run(body: PhysicsBodyComponent) =
        let gate = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_GATE")
        if owner.Name = "B2Dynamic" && not failed && block < 16 && Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_BENCH") = "1"
           && (String.IsNullOrEmpty(gate) || File.Exists(gate)) then
            try
                let mutable state = Unchecked.defaultof<PhysicsBodyState>
                if obj.ReferenceEquals(body, null) || body.ReadState(&state) <> PhysicsError.None then invalidOp "benchmark body state"
                if block = 0 && body.SetVelocity(Float3(1.0f, 0.0f, 0.0f)) <> PhysicsError.None then invalidOp "benchmark moving body"
                let count = if block < 8 then 16 else 64
                let order = block % 8
                let batch = order = 1 || order = 2 || order = 5 || order = 6
                let requests = Array.zeroCreate<PhysicsQueryRequest> count
                let scalarHits = Array.zeroCreate<PhysicsHit> (count * 8)
                let batchHits = Array.zeroCreate<PhysicsHit> (count * 8)
                let results = Array.zeroCreate<PhysicsBatchResult> count
                let stress = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_STRESS") = "1"
                let point = if stress then Float3(100.0f, 0.0f, 0.0f) else state.Position + owner.Transform.Right * 1.5f
                let radius = if stress then 4.0f else 0.05f
                for i in 0 .. count - 1 do
                    requests[i] <-
                        if i % 2 = 0 then PhysicsQueryRequest.Raycast(point + Float3(0.0f, 5.0f, 0.0f), Float3(0.0f, -1.0f, 0.0f), 10.0f, i * 8, 8)
                        else PhysicsQueryRequest.OverlapSphere(point, radius, i * 8, 8)
                if Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(batchHits), Span(results)) <> PhysicsError.None then invalidOp "benchmark parity batch"
                let mutable own = false
                for i in 0 .. count - 1 do
                    let scalar =
                        if i % 2 = 0 then Physics.Raycast(owner, requests[i].Origin, requests[i].Direction, 10.0f, Span(scalarHits, i * 8, 8))
                        else Physics.OverlapSphere(owner, point, radius, Span(scalarHits, i * 8, 8))
                    if not scalar.Succeeded || not results[i].Succeeded || scalar.Written <> results[i].Written
                       || scalar.RequiredCapacity <> results[i].RequiredCapacity || scalar.Truncated <> results[i].Truncated then
                        invalidOp "benchmark result counts"
                    for j in 0 .. scalar.Written - 1 do
                        let hit = scalarHits[i * 8 + j]
                        let mutable matched = false
                        for k in 0 .. results[i].Written - 1 do
                            let other = batchHits[i * 8 + k]
                            matched <- matched || (hit.Entity = other.Entity && hit.ComponentId = other.ComponentId && hit.ShapeId = other.ShapeId && abs (hit.Distance - other.Distance) < 0.0001f)
                        if not matched then invalidOp "benchmark result identity"
                        if i % 2 <> 0 then own <- own || hit.Entity = owner
                if stress then
                    let moving = Array.zeroCreate<PhysicsHit> 8
                    let collision = Physics.OverlapSphere(owner, state.Position + owner.Transform.Right * 1.5f, 0.05f, Span(moving))
                    for i in 0 .. collision.Written - 1 do own <- own || moving[i].Entity = owner
                    for i in 1 .. 2 .. count - 1 do
                        if results[i].Written <> 8 || results[i].RequiredCapacity <> 128 || not results[i].Truncated then invalidOp "benchmark stress overflow"
                if not own then invalidOp "benchmark moving collision missing"
                let boundedCapture = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_BOUNDED_CAPTURE") = "1"
                let samples = Array.zeroCreate<double> (if boundedCapture then 20 else 600)
                let mutable cpuStart = struct (0UL, 0UL, 0u)
                let mutable blockStart = 0L
                for sample in (if boundedCapture then -2 else -60) .. samples.Length - 1 do
                    if sample = 0 then
                        cpuStart <- QueryCpu.read()
                        blockStart <- Stopwatch.GetTimestamp()
                    let start = Stopwatch.GetTimestamp()
                    if batch then
                        if Physics.QueryBatch(owner, ReadOnlySpan(requests), Span(batchHits), Span(results)) <> PhysicsError.None then invalidOp "benchmark timed batch"
                    else
                        for i in 0 .. count - 1 do
                            let answer =
                                if i % 2 = 0 then Physics.Raycast(owner, requests[i].Origin, requests[i].Direction, 10.0f, Span(scalarHits, i * 8, 8))
                                else Physics.OverlapSphere(owner, point, radius, Span(scalarHits, i * 8, 8))
                            if not answer.Succeeded then invalidOp "benchmark timed scalar"
                    let elapsed = Stopwatch.GetTimestamp() - start
                    if sample >= 0 then samples[sample] <- double elapsed * 1000000.0 / double Stopwatch.Frequency
                let blockEnd = Stopwatch.GetTimestamp()
                let struct (endTime, endCycles, endThread) = QueryCpu.read()
                let struct (startTime, startCycles, startThread) = cpuStart
                if startThread <> endThread || endTime < startTime || endCycles < startCycles then invalidOp "benchmark CPU accounting identity"
                let blockWallUs = double (blockEnd - blockStart) * 1000000.0 / double Stopwatch.Frequency
                let threadCpuUs = double (endTime - startTime) / 10.0
                let threadCycles = endCycles - startCycles
                let rawUs = Array.copy samples
                let mean = Array.average samples
                Array.sortInPlace samples
                Console.WriteLine("[physics.player.query-benchmark] " + JsonSerializer.Serialize({| boundedCapture = boundedCapture; warmup = (if boundedCapture then 2 else 60);
                    workload = (if stress then "dense128" else "moving-dagger"); overlapRequired = (if stress then 128 else 1);
                    block = block; requests = count; mode = (if batch then "batch" else "scalar"); samples = samples.Length; meanUs = mean;
                    p99Us = samples[int (Math.Ceiling(double samples.Length * 0.99)) - 1]; positionX = state.Position.X;
                    ownerThreadId = endThread; blockWallUs = blockWallUs; threadCpuUs = threadCpuUs; threadCycles = threadCycles; rawUs = rawUs;
                    cpuAccounting = "GetThreadTimes kernel+user; QueryThreadCycleTime; block includes loop/timer overhead";
                    parity = true; movingCollision = own; profile = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_PROFILE") = "1" |}))
                block <- block + 1
            with error ->
                failed <- true
                Console.WriteLine("[physics.player.query-benchmark.failure] " + error.Message)
