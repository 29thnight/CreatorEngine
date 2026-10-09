namespace CreatorEngine.Scripts;

public sealed partial class PhysicsB2PlayerProbe : Component
{
    private PhysicsBodyComponent? _body;
    private int _phase, _passed;
    private float _elapsed, _changedAt, _expectedX;
    private bool _finished;

    private bool Dynamic => Entity.Name == "B2Dynamic";
    private bool Kinematic => Entity.Name == "B2Kinematic";

    private void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
        ++_passed;
    }

    public override void PostPhysics(float delta)
    {
        if (_finished)
        {
            RunDynamicQueries(delta);
            RunMixedQueries(delta);

            RunQueryBenchmark();
            return;
        }

        try
        {
            _elapsed += delta;
            _body ??= GetComponent<PhysicsBodyComponent>();
            if (_body is null || _body.ReadState(out var state) != PhysicsError.None)
                throw new InvalidOperationException("body lookup/read");

            if (_phase == 0)
            {
                var kind = Dynamic ? PhysicsBodyKind.Dynamic : Kinematic ? PhysicsBodyKind.Kinematic : PhysicsBodyKind.Static;
                Check(state.Kind == kind, "body kind");
                _phase = 1;
            }
            if (_phase == 1 && _elapsed >= .2f)
            {
                _expectedX = Dynamic ? 4 : Kinematic ? 30 : 20;
                Transform.WorldPosition = new Float3(_expectedX, 5, 0);
                _changedAt = _elapsed;
                _phase = 2;
            }
            else if (_phase == 2 && _elapsed >= _changedAt + .1f)
            {
                Check(state.Position.X >= _expectedX-.01f && state.Position.X < _expectedX+(Dynamic ? 1.5f : .01f), "authored world pose reaches SDK");
                CheckPose(state);
                _expectedX = state.Position.X + 2;
                var parent = Entity.Parent.Transform!;
                parent.LocalPosition += new Float3(2,0,0);
                _changedAt = _elapsed;
                _phase = 3;
            }
            else if (_phase == 3 && _elapsed >= _changedAt + .1f)
            {
                Check(state.Position.X >= _expectedX-.01f && state.Position.X < _expectedX+(Dynamic ? 1.5f : .01f), "parent translation reaches SDK");
                CheckPose(state);
                Entity.Parent.Transform!.LocalScale = new Float3(3,3,3);
                _expectedX = Transform.WorldPosition.X;
                _changedAt = _elapsed;
                _phase = 4;
            }
            else if (_phase == 4 && _elapsed >= _changedAt + .1f)
            {
                Check(state.Position.X >= _expectedX-.01f && state.Position.X < _expectedX+(Dynamic ? 1.5f : .01f), "parent scale preserves world pose policy");
                CheckPose(state);
                Span<PhysicsHit> hits = stackalloc PhysicsHit[8];
                var offset = Transform.Right * (Dynamic ? 1.5f : 1.4f);
                var query = Physics.OverlapSphere(Entity, state.Position+offset, .05f, hits);
                bool own = false;
                for(int i=0; i<query.Written; ++i) own |= hits[i].Entity.Equals(Entity);
                Check(query.Succeeded && own, "parent scale updates actual collision geometry");
                if (Dynamic)
                    Check(_body.SetVelocity(new Float3(0,0,0)) == PhysicsError.None, "halt dynamic fixture");
                else
                    Check(state.Kind == (Kinematic ? PhysicsBodyKind.Kinematic : PhysicsBodyKind.Static), "motion kind preserved during shape replacement");
                CheckBatch(state);
                CheckDenseBatch(state);
                if (Dynamic && Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_STRESS") == "1") CheckStress();
                _finished = true;
                Console.WriteLine($"[physics.player.b2] {{\"role\":\"{Entity.Name}\",\"passed\":{_passed},\"failed\":0,\"seconds\":{_elapsed},\"x\":{state.Position.X},\"complete\":true}}");
            }
        }
        catch(Exception error)
        {
            _finished = true;
            Console.WriteLine($"[physics.player.b2] {{\"role\":\"{Entity.Name}\",\"passed\":{_passed},\"failed\":1,\"complete\":false}}");
            Console.WriteLine($"[physics.player.b2.failure] {Entity.Name}: {error.Message}");
        }
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentThread();

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetThreadTimes(IntPtr thread, out ulong creation, out ulong exit, out ulong kernel, out ulong user);

    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool QueryThreadCycleTime(IntPtr thread, out ulong cycles);

    private readonly record struct CpuMark(ulong Time100ns, ulong Cycles, uint ThreadId);

    private static CpuMark ReadCpuMark()
    {
        var thread = GetCurrentThread();
        if (!GetThreadTimes(thread, out _, out _, out ulong kernel, out ulong user) ||
            !QueryThreadCycleTime(thread, out ulong cycles))
            throw new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error());

        return new CpuMark(kernel + user, cycles, GetCurrentThreadId());
    }

    private int _queryBenchmarkBlock;
    private bool _queryBenchmarkFailed;

    private void RunQueryBenchmark()
    {
        if (!Dynamic || _queryBenchmarkFailed || _queryBenchmarkBlock >= 16 ||
            Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_BENCH") != "1") return;

        var gate = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_GATE");
        if (!string.IsNullOrEmpty(gate) && !System.IO.File.Exists(gate)) return;

        try
        {
            if (_body is null || _body.ReadState(out var state) != PhysicsError.None)
                throw new InvalidOperationException("benchmark body state");

            if (_queryBenchmarkBlock == 0 && _body.SetVelocity(new Float3(1, 0, 0)) != PhysicsError.None)
                throw new InvalidOperationException("benchmark moving body");

            int count = _queryBenchmarkBlock < 8 ? 16 : 64;
            int order = _queryBenchmarkBlock % 8;
            bool batch = order is 1 or 2 or 5 or 6;
            Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[count];
            Span<PhysicsHit> scalarHits = stackalloc PhysicsHit[count * 8];
            Span<PhysicsHit> batchHits = stackalloc PhysicsHit[count * 8];
            Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[count];
            bool stress = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_STRESS") == "1";
            var point = stress ? new Float3(100, 0, 0) : state.Position + Transform.Right * 1.5f;
            float radius = stress ? 4f : .05f;

            for (int i = 0; i < count; ++i)
                requests[i] = i % 2 == 0
                    ? PhysicsQueryRequest.Raycast(point + new Float3(0, 5, 0), new Float3(0, -1, 0), 10, i * 8, 8)
                    : PhysicsQueryRequest.OverlapSphere(point, radius, i * 8, 8);

            void ValidateCounts(PhysicsQueryResult scalar, PhysicsBatchResult grouped)
            {
                if (!scalar.Succeeded || !grouped.Succeeded || scalar.Written != grouped.Written ||
                    scalar.RequiredCapacity != grouped.RequiredCapacity || scalar.Truncated != grouped.Truncated)
                    throw new InvalidOperationException("benchmark result counts");
            }

            if (Physics.QueryBatch(Entity, requests, batchHits, results) != PhysicsError.None)
                throw new InvalidOperationException("benchmark parity batch");
            bool own = false;
            for (int i = 0; i < count; ++i)
            {
                var scalar = i % 2 == 0
                    ? Physics.Raycast(Entity, requests[i].Origin, requests[i].Direction, 10, scalarHits.Slice(i * 8, 8))
                    : Physics.OverlapSphere(Entity, point, radius, scalarHits.Slice(i * 8, 8));
                ValidateCounts(scalar, results[i]);
                for (int j = 0; j < scalar.Written; ++j)
                {
                    var hit = scalarHits[i * 8 + j];
                    bool matched = false;
                    for (int k = 0; k < results[i].Written; ++k)
                    {
                        var other = batchHits[i * 8 + k];
                        matched |= hit.Entity.Equals(other.Entity) && hit.ComponentId == other.ComponentId &&
                            hit.ShapeId == other.ShapeId && MathF.Abs(hit.Distance - other.Distance) < .0001f;
                    }
                    if (!matched) throw new InvalidOperationException("benchmark result identity");
                    if (i % 2 != 0) own |= hit.Entity.Equals(Entity);
                }
            }
            if (stress)
            {
                Span<PhysicsHit> moving = stackalloc PhysicsHit[8];
                var collision = Physics.OverlapSphere(Entity, state.Position + Transform.Right * 1.5f, .05f, moving);
                for (int i = 0; i < collision.Written; ++i) own |= moving[i].Entity.Equals(Entity);
                for (int i = 1; i < count; i += 2)
                    if (results[i].Written != 8 || results[i].RequiredCapacity != 128 || !results[i].Truncated)
                        throw new InvalidOperationException("benchmark stress overflow");
            }
            if (!own) throw new InvalidOperationException("benchmark moving collision missing");

            // Caller storage and requests are prepared outside the timed interval.
            bool boundedCapture = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_BOUNDED_CAPTURE") == "1";
            var samples = new double[boundedCapture ? 20 : 600];
            CpuMark cpuStart = default;
            long blockStart = 0;
            for (int sample = boundedCapture ? -2 : -60; sample < samples.Length; ++sample)
            {
                if (sample == 0)
                {
                    cpuStart = ReadCpuMark();
                    blockStart = System.Diagnostics.Stopwatch.GetTimestamp();
                }

                long start = System.Diagnostics.Stopwatch.GetTimestamp();
                if (batch)
                {
                    if (Physics.QueryBatch(Entity, requests, batchHits, results) != PhysicsError.None)
                        throw new InvalidOperationException("benchmark timed batch");
                }
                else
                {
                    for (int i = 0; i < count; ++i)
                    {
                        var answer = i % 2 == 0
                            ? Physics.Raycast(Entity, requests[i].Origin, requests[i].Direction, 10, scalarHits.Slice(i * 8, 8))
                            : Physics.OverlapSphere(Entity, point, radius, scalarHits.Slice(i * 8, 8));
                        if (!answer.Succeeded) throw new InvalidOperationException("benchmark timed scalar");
                    }
                }
                long elapsed = System.Diagnostics.Stopwatch.GetTimestamp() - start;
                if (sample >= 0) samples[sample] = elapsed * 1_000_000.0 / System.Diagnostics.Stopwatch.Frequency;
            }
            long blockEnd = System.Diagnostics.Stopwatch.GetTimestamp();
            var cpuEnd = ReadCpuMark();
            if (cpuStart.ThreadId != cpuEnd.ThreadId || cpuEnd.Time100ns < cpuStart.Time100ns || cpuEnd.Cycles < cpuStart.Cycles)
                throw new InvalidOperationException("benchmark CPU accounting identity");

            double blockWallUs = (blockEnd - blockStart) * 1_000_000.0 / System.Diagnostics.Stopwatch.Frequency;
            double threadCpuUs = (cpuEnd.Time100ns - cpuStart.Time100ns) / 10.0;
            ulong threadCycles = cpuEnd.Cycles - cpuStart.Cycles;
            var rawUs = (double[])samples.Clone();
            double mean = samples.Average();
            Array.Sort(samples);
            Console.WriteLine("[physics.player.query-benchmark] " + System.Text.Json.JsonSerializer.Serialize(new
            {
                boundedCapture, warmup = boundedCapture ? 2 : 60,
                workload = stress ? "dense128" : "moving-dagger", overlapRequired = stress ? 128 : 1,
                block = _queryBenchmarkBlock, requests = count, mode = batch ? "batch" : "scalar",
                samples = samples.Length, meanUs = mean, p99Us = samples[(int)Math.Ceiling(samples.Length * .99) - 1], positionX = state.Position.X,
                ownerThreadId = cpuEnd.ThreadId, blockWallUs, threadCpuUs, threadCycles, rawUs,
                cpuAccounting = "GetThreadTimes kernel+user; QueryThreadCycleTime; block includes loop/timer overhead",
                parity = true, movingCollision = own, profile = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_PROFILE") == "1"
            }));
            ++_queryBenchmarkBlock;
        }
        catch (Exception error)
        {
            _queryBenchmarkFailed = true;
            Console.WriteLine("[physics.player.query-benchmark.failure] " + error.Message);
        }
    }

    private void CheckBatch(PhysicsBodyState state)
    {
        int passed = 0;
        void BatchCheck(bool value, string message)
        {
            if (!value) throw new InvalidOperationException("batch: " + message);
            ++passed;
        }

        Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[4];
        Span<PhysicsHit> hits = stackalloc PhysicsHit[16];
        Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[4];
        var queryPoint = state.Position + Transform.Right * (Dynamic ? 1.5f : 1.4f);
        requests[0] = PhysicsQueryRequest.OverlapSphere(queryPoint, .05f, 0, 8);
        requests[1] = PhysicsQueryRequest.Raycast(state.Position, default, 10, 8, 0);
        requests[2] = PhysicsQueryRequest.OverlapSphere(queryPoint, .05f, 8, 0);
        requests[3] = PhysicsQueryRequest.Raycast(state.Position + new Float3(1000000,20,0), new Float3(0,-1,0), 40, 8, 8);
        BatchCheck(Physics.QueryBatch(Entity, requests, hits, results) == PhysicsError.None, "mixed read window");
        bool own = false;
        for (int i = 0; i < results[0].Written; ++i) own |= hits[i].Entity.Equals(Entity);
        BatchCheck(results[0].Succeeded && own, "overlap entity ownership");
        BatchCheck(results[1].Error == PhysicsError.InvalidArgument && results[1].Written == 0, "individual error");
        BatchCheck(results[2].Succeeded && results[2].RequiredCapacity > 0 && results[2].Truncated, "capacity discovery");
        BatchCheck(results[3].Succeeded && results[3].Written == 0, "no-hit ray after invalid request");
        var before = results[0];
        requests[3] = PhysicsQueryRequest.Raycast(state.Position, new Float3(0,-1,0), 40, 0, 8);
        BatchCheck(Physics.QueryBatch(Entity, requests, hits, results) == PhysicsError.InvalidArgument, "overlap ranges rejected");
        BatchCheck(results[0].Written == before.Written && results[0].Error == before.Error, "batch error preserves outputs");
        requests[3] = PhysicsQueryRequest.Raycast(state.Position, new Float3(0,-1,0), 40, 8, 8);
        var aliased = System.Runtime.InteropServices.MemoryMarshal.Cast<PhysicsHit, PhysicsBatchResult>(hits).Slice(0,4);
        var savedComponent = hits[0].ComponentId;
        BatchCheck(Physics.QueryBatch(Entity, requests, hits, aliased) == PhysicsError.InvalidArgument, "cross-buffer alias rejected");
        BatchCheck(hits[0].ComponentId == savedComponent, "alias failure preserves hits");
        BatchCheck(Physics.QueryBatch(Entity, requests, hits, results.Slice(0,1)) == PhysicsError.InvalidArgument, "summary length mismatch");
        BatchCheck(Physics.QueryBatch(Entity, ReadOnlySpan<PhysicsQueryRequest>.Empty, Span<PhysicsHit>.Empty,
                                     Span<PhysicsBatchResult>.Empty) == PhysicsError.None, "empty owner batch");
        requests[3] = PhysicsQueryRequest.Raycast(state.Position, new Float3(0,-1,0), 40, -1, 1);
        BatchCheck(Physics.QueryBatch(Entity, requests, hits, results) == PhysicsError.InvalidArgument, "negative offset rejected");
        requests[3] = PhysicsQueryRequest.Raycast(state.Position, new Float3(0,-1,0), 40, int.MaxValue, 1);
        BatchCheck(Physics.QueryBatch(Entity, requests, hits, results) == PhysicsError.InvalidArgument, "offset overflow rejected");
        Span<PhysicsQueryRequest> maximum = stackalloc PhysicsQueryRequest[Physics.MaxBatchRequests];
        Span<PhysicsBatchResult> maximumResults = stackalloc PhysicsBatchResult[Physics.MaxBatchRequests];
        for (int i = 0; i < maximum.Length; ++i)
            maximum[i] = PhysicsQueryRequest.Raycast(state.Position + new Float3(1000000,20,0), new Float3(0,-1,0), 40, 0, 0);
        BatchCheck(Physics.QueryBatch(Entity, maximum, Span<PhysicsHit>.Empty, maximumResults) == PhysicsError.None, "maximum batch accepted");
        bool allEmpty = true;
        foreach (var result in maximumResults) allEmpty &= result.Succeeded && result.Written == 0 && result.RequiredCapacity == 0;
        BatchCheck(allEmpty, "all maximum batch slots written");
        BatchCheck(Physics.QueryBatch(Entity, new PhysicsQueryRequest[Physics.MaxBatchRequests+1], Span<PhysicsHit>.Empty,
                                     new PhysicsBatchResult[Physics.MaxBatchRequests+1]) == PhysicsError.InvalidArgument, "maximum request overflow rejected");
        Console.WriteLine($"[physics.player.batch] {{\"role\":\"{Entity.Name}\",\"passed\":{passed},\"failed\":0,\"complete\":true}}");
    }

    private void CheckDenseBatch(PhysicsBodyState state)
    {
        int passed = 0;
        void DenseCheck(bool value, string message)
        {
            if (!value) throw new InvalidOperationException("dense batch: " + message);
            ++passed;
        }

        Span<PhysicsHit> hits = new PhysicsHit[Physics.MaxBatchHitCapacity];
        System.Runtime.InteropServices.MemoryMarshal.AsBytes(hits).Fill(0x5a);
        Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[5];
        Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[5];
        var point = state.Position + Transform.Right * (Dynamic ? 1.5f : 1.4f);
        requests[0] = PhysicsQueryRequest.OverlapSphere(point, .05f, 4000, 8);
        requests[1] = PhysicsQueryRequest.Raycast(state.Position, default, 10, 32, 8);
        requests[2] = PhysicsQueryRequest.OverlapSphere(point, .05f, 0, 1);
        requests[3] = PhysicsQueryRequest.Raycast(state.Position + new Float3(1000000,20,0), new Float3(0,-1,0), 40, 1024, 8);
        requests[4] = PhysicsQueryRequest.OverlapSphere(point, .05f, 64, 0);
        DenseCheck(Physics.QueryBatch(Entity, requests, hits, results) == PhysicsError.None, "sparse reverse offsets");
        bool own = false;
        for (int i = 0; i < results[0].Written; ++i) own |= hits[4000 + i].Entity.Equals(Entity);
        DenseCheck(results[0].Succeeded && own, "high output offset identity");
        DenseCheck(results[2].Succeeded && results[2].Written == 1 && hits[0].Entity.Equals(Entity), "request order differs from output order");
        DenseCheck(results[1].Error == PhysicsError.InvalidArgument && results[1].Written == 0, "individual error between successful requests");
        DenseCheck(results[3].Succeeded && results[3].Written == 0 && results[4].Succeeded && results[4].Written == 0 && results[4].RequiredCapacity > 0 && results[4].Truncated, "no-hit and capacity discovery");
        bool preserved = true;
        for (int i = 0; i < hits.Length; ++i)
        {
            if (i < results[2].Written || (i >= 4000 && i < 4000 + results[0].Written)) continue;
            foreach (byte value in System.Runtime.InteropServices.MemoryMarshal.AsBytes(hits.Slice(i,1))) preserved &= value == 0x5a;
        }
        DenseCheck(preserved, "all gaps, unwritten tails and failed request bytes preserved");
        Console.WriteLine($"[physics.player.dense-batch] {{\"role\":\"{Entity.Name}\",\"passed\":{passed},\"failed\":0,\"complete\":true}}");
    }

    private void CheckStress()
    {
        int passed = 0;
        void StressCheck(bool value, string message)
        {
            if (!value) throw new InvalidOperationException("stress: " + message);
            ++passed;
        }

        Span<PhysicsHit> full = new PhysicsHit[256];
        var scalar = Physics.OverlapSphere(Entity, new Float3(100,0,0), 4, full);
        StressCheck(scalar.Succeeded && scalar.Written == 128 && scalar.RequiredCapacity == 128 && !scalar.Truncated, "full density count");
        var ids = new HashSet<ulong>();
        for (int i = 0; i < scalar.Written; ++i)
        {
            if (!full[i].Entity.Name.StartsWith("DenseQuery", StringComparison.Ordinal)) throw new InvalidOperationException("stress ownership");
            ids.Add(full[i].ComponentId);
        }
        StressCheck(ids.Count == 128, "distinct body identity");
        Span<PhysicsHit> hits = new PhysicsHit[Physics.MaxBatchHitCapacity];
        System.Runtime.InteropServices.MemoryMarshal.AsBytes(hits).Fill(0x5a);
        Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[5];
        Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[5];
        requests[0] = PhysicsQueryRequest.OverlapSphere(new Float3(100,0,0),4,1024,256);
        requests[1] = PhysicsQueryRequest.OverlapSphere(new Float3(100,0,0),4,0,8);
        requests[2] = PhysicsQueryRequest.OverlapSphere(new Float3(100,0,0),4,32,0);
        requests[3] = PhysicsQueryRequest.Raycast(new Float3(100,0,0),default,10,4000,8);
        requests[4] = PhysicsQueryRequest.Raycast(new Float3(1000000,20,0),new Float3(0,-1,0),40,2048,8);
        StressCheck(Physics.QueryBatch(Entity,requests,hits,results) == PhysicsError.None, "mixed density batch");
        StressCheck(results[0].Succeeded && results[0].Written == 128 && results[0].RequiredCapacity == 128 && !results[0].Truncated, "full batch count");
        StressCheck(results[1].Written == 8 && results[1].RequiredCapacity == 128 && results[1].Truncated && results[2].Written == 0 && results[2].RequiredCapacity == 128 && results[2].Truncated, "overflow and discovery");
        StressCheck(results[3].Error == PhysicsError.InvalidArgument && results[4].Succeeded && results[4].Written == 0, "error and no-hit after overflow");
        var batchIds = new HashSet<ulong>();
        for(int i=0;i<128;++i) batchIds.Add(hits[1024+i].ComponentId);
        bool preserved = batchIds.SetEquals(ids);
        for(int i=0;i<8;++i) preserved &= ids.Contains(hits[i].ComponentId);
        for(int i=0;i<hits.Length;++i)
        {
            if(i<8 || (i>=1024 && i<1152)) continue;
            foreach(byte value in System.Runtime.InteropServices.MemoryMarshal.AsBytes(hits.Slice(i,1))) preserved &= value == 0x5a;
        }
        StressCheck(preserved, "identity and all untouched bytes");
        Console.WriteLine($"[physics.player.stress] {{\"passed\":{passed},\"failed\":0,\"bodies\":128,\"capacity\":4096,\"written\":136,\"complete\":true}}");

        CheckMaximumBatch(ids);
    }

    private void CheckMaximumBatch(HashSet<ulong> expectedIds)
    {
        int passed = 0;
        void MaximumCheck(bool value, string message)
        {
            if (!value) throw new InvalidOperationException("maximum batch: " + message);

            ++passed;
        }

        Span<PhysicsHit> backing = new PhysicsHit[Physics.MaxBatchHitCapacity + 2];
        System.Runtime.InteropServices.MemoryMarshal.AsBytes(backing).Fill(0x5a);

        Span<PhysicsHit> hits = backing.Slice(1, Physics.MaxBatchHitCapacity);
        Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[64];
        Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[64];

        for (int i = 0; i < requests.Length; ++i)
        {
            requests[i] = PhysicsQueryRequest.OverlapSphere(new Float3(100,0,0), 4, (63-i)*64, 64);
        }

        MaximumCheck(Physics.QueryBatch(Entity, requests, hits, results) == PhysicsError.None, "maximum request and hit capacity");

        int written = 0;
        bool counts = true;
        bool identities = true;

        for (int i = 0; i < results.Length; ++i)
        {
            counts &= results[i].Succeeded && results[i].Written == 64 && results[i].RequiredCapacity == 128 && results[i].Truncated;
            written += results[i].Written;

            var requestIds = new HashSet<ulong>();

            for (int j = 0; j < 64; ++j)
            {
                var hit = hits[(63-i)*64+j];
                identities &= expectedIds.Contains(hit.ComponentId) && hit.Entity.Name.StartsWith("DenseQuery", StringComparison.Ordinal);
                requestIds.Add(hit.ComponentId);
            }

            identities &= requestIds.Count == 64;
        }

        MaximumCheck(counts && written == 4096, "every request count and truncation");
        MaximumCheck(identities, "every output identity and per-request uniqueness");

        bool guards = true;

        foreach (byte value in System.Runtime.InteropServices.MemoryMarshal.AsBytes(backing.Slice(0, 1))) guards &= value == 0x5a;
        foreach (byte value in System.Runtime.InteropServices.MemoryMarshal.AsBytes(backing.Slice(backing.Length-1, 1))) guards &= value == 0x5a;

        MaximumCheck(guards, "both output boundary guards preserved");

        Console.WriteLine($"[physics.player.max-batch] {{\"passed\":{passed},\"failed\":0,\"requests\":64,\"capacity\":4096,\"written\":{written},\"guardBytes\":128,\"complete\":true}}");
    }

    private void CheckPose(PhysicsBodyState state)
    {
        var position = Transform.WorldPosition;
        float x=position.X-state.Position.X, y=position.Y-state.Position.Y, z=position.Z-state.Position.Z;
        Check(x*x+y*y+z*z < 1e-5f, "authoritative Transform matches completed body under rotated/scaled parent");
    }
}
