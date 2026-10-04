namespace CreatorEngine.Scripts;

public sealed partial class PhysicsB2PlayerProbe
{
    private PhysicsBodyComponent[]? _queryBodies;
    private Float3[]? _queryInitial;
    private readonly HashSet<ulong> _queryIds = new();
    private int _queryMotionPhase, _queryMotionPassed, _queryMotionReads, _queryMotionStages;
    private float _queryMotionSeconds, _queryPreviousDisplacement;
    private int _queryPoseChanges;
    private Float3 _queryFarCenter;
    private bool _queryMotionFinished;

    private void MotionCheck(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);

        ++_queryMotionPassed;
    }

    private void RunDynamicQueries(float delta)
    {
        if (!Dynamic || _queryMotionFinished || Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_DYNAMIC") != "1") return;

        try
        {
            _queryMotionSeconds += delta;

            if (_queryMotionSeconds > 5) throw new InvalidOperationException("dynamic query motion timed out");

            if (_queryBodies is null)
            {
                _queryBodies = new PhysicsBodyComponent[128];
                _queryInitial = new Float3[128];

                for (int i = 0; i < 128; ++i)
                {
                    var owner = Entity.Find($"DenseQuery{i:D3}");
                    var body = owner.GetComponent<PhysicsBodyComponent>();

                    if (body is null || body.ReadState(out var state) != PhysicsError.None || state.Kind != PhysicsBodyKind.Dynamic)
                        throw new InvalidOperationException("dynamic body lookup/kind");

                    _queryBodies[i] = body;
                    _queryInitial[i] = state.Position;
                    _queryIds.Add(body.ComponentId);
                }

                MotionCheck(_queryIds.Count == 128, "128 distinct dynamic body identities");
                ValidateDynamicStage(_queryInitial[0]);
                SetQueryVelocity(20);
                _queryMotionPhase = 1;

                return;
            }

            if (_queryBodies[0].ReadState(out var anchor) != PhysicsError.None) throw new InvalidOperationException("moving anchor read");

            float displacement = anchor.Position.X - _queryInitial![0].X;
            if (MathF.Abs(displacement - _queryPreviousDisplacement) > .0001f) ++_queryPoseChanges;

            _queryPreviousDisplacement = displacement;
            bool coherent = true;

            for (int i = 0; i < 128; ++i)
            {
                if (_queryBodies[i].ReadState(out var state) != PhysicsError.None) throw new InvalidOperationException("moving body read");

                coherent &= state.Kind == PhysicsBodyKind.Dynamic &&
                    MathF.Abs(state.Position.X - _queryInitial[i].X - displacement) < .002f &&
                    MathF.Abs(state.Position.Y - _queryInitial[i].Y) < .002f &&
                    MathF.Abs(state.Position.Z - _queryInitial[i].Z) < .002f;

                if (_queryMotionPhase == 3) coherent &= MathF.Abs(state.LinearVelocity.X) < .001f;
            }

            MotionCheck(coherent, "coherent dynamic motion and gravity-disabled pose");
            ValidateDynamicHits(anchor.Position);
            ++_queryMotionReads;

            if (_queryMotionPhase == 1 && displacement >= 12)
            {
                _queryFarCenter = anchor.Position;
                MotionCheck(DynamicCount(_queryInitial[0]) == 0, "departed query volume is empty");
                ValidateDynamicStage(anchor.Position);
                SetQueryVelocity(-20);
                _queryMotionPhase = 2;
            }
            else if (_queryMotionPhase == 2 && displacement <= .02f)
            {
                MotionCheck(DynamicCount(_queryInitial[0]) == 128 && DynamicCount(_queryFarCenter) == 0, "return updates both query volumes");
                ValidateDynamicStage(anchor.Position);
                SetQueryVelocity(0);
                _queryMotionPhase = 3;
            }
            else if (_queryMotionPhase == 3)
            {
                MotionCheck(_queryMotionReads >= 2 && _queryPoseChanges >= 2 && _queryMotionStages == 3, "all motion stages completed");
                _queryMotionFinished = true;

                Console.WriteLine($"[physics.player.dynamic-query] {{\"passed\":{_queryMotionPassed},\"failed\":0,\"bodies\":128,\"readWindows\":{_queryMotionReads},\"observedPoseChanges\":{_queryPoseChanges},\"stages\":3,\"writtenPerFullBatch\":4096,\"departedHits\":0,\"returnedHits\":128,\"stoppedBodies\":128,\"complete\":true}}");
            }
        }
        catch (Exception error)
        {
            _queryMotionFinished = true;
            Console.WriteLine($"[physics.player.dynamic-query.failure] {error.Message}");
        }
    }

    private void SetQueryVelocity(float velocity)
    {
        foreach (var body in _queryBodies!)
            if (body.SetVelocity(new Float3(velocity,0,0)) != PhysicsError.None) throw new InvalidOperationException("dynamic velocity command");
    }

    private int DynamicCount(Float3 point)
    {
        Span<PhysicsHit> hits = new PhysicsHit[256];
        var result = Physics.OverlapSphere(Entity, point, 4, hits);

        if (!result.Succeeded || result.Truncated || result.Written != result.RequiredCapacity) throw new InvalidOperationException("dynamic scalar count");

        return result.Written;
    }

    private void ValidateDynamicHits(Float3 point)
    {
        Span<PhysicsHit> hits = new PhysicsHit[256];
        var result = Physics.OverlapSphere(Entity, point, 4, hits);
        var ids = new HashSet<ulong>();

        for (int i = 0; i < result.Written; ++i) ids.Add(hits[i].ComponentId);

        MotionCheck(result.Succeeded && result.Written == 128 && result.RequiredCapacity == 128 && !result.Truncated && ids.SetEquals(_queryIds), "moving scalar query retains all identities");
    }

    private void ValidateDynamicStage(Float3 point)
    {
        Span<PhysicsHit> backing = new PhysicsHit[4098];
        System.Runtime.InteropServices.MemoryMarshal.AsBytes(backing).Fill(0x5a);
        var hits = backing.Slice(1, 4096);
        Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[32];
        Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[32];

        for (int i = 0; i < 32; ++i) requests[i] = PhysicsQueryRequest.OverlapSphere(point,4,(31-i)*128,128);

        MotionCheck(Physics.QueryBatch(Entity,requests,hits,results) == PhysicsError.None, "moving full-output batch");
        bool correct = true;

        for (int i = 0; i < 32; ++i)
        {
            correct &= results[i].Succeeded && results[i].Written == 128 && results[i].RequiredCapacity == 128 && !results[i].Truncated;
            var ids = new HashSet<ulong>();

            for (int j = 0; j < 128; ++j) ids.Add(hits[(31-i)*128+j].ComponentId);

            correct &= ids.SetEquals(_queryIds);
        }

        MotionCheck(correct, "all 4096 moving output identities and counts");
        bool guarded = true;

        foreach (byte value in System.Runtime.InteropServices.MemoryMarshal.AsBytes(backing.Slice(0,1))) guarded &= value == 0x5a;
        foreach (byte value in System.Runtime.InteropServices.MemoryMarshal.AsBytes(backing.Slice(4097,1))) guarded &= value == 0x5a;

        MotionCheck(guarded, "moving full-output boundary guards");

        for (int i = 0; i < 16; ++i)
            requests[i] = i % 2 == 0
                ? PhysicsQueryRequest.Raycast(point+new Float3(0,5,0),new Float3(0,-1,0),10,i*256,256)
                : PhysicsQueryRequest.OverlapSphere(point,4,i*256,256);

        MotionCheck(Physics.QueryBatch(Entity,requests.Slice(0,16),hits,results.Slice(0,16)) == PhysicsError.None, "moving mixed batch");

        for (int i = 0; i < 16; ++i)
        {
            Span<PhysicsHit> scalar = new PhysicsHit[256];
            var answer = i % 2 == 0
                ? Physics.Raycast(Entity,point+new Float3(0,5,0),new Float3(0,-1,0),10,scalar)
                : Physics.OverlapSphere(Entity,point,4,scalar);
            var ids = new HashSet<ulong>();

            for (int j = 0; j < answer.Written; ++j) ids.Add(scalar[j].ComponentId);

            var groupedIds = new HashSet<ulong>();
            bool metadata = true;

            for (int j = 0; j < results[i].Written; ++j) groupedIds.Add(hits[i*256+j].ComponentId);

            for (int j = 0; j < answer.Written; ++j)
            {
                bool matched = false;

                for (int k = 0; k < results[i].Written; ++k)
                {
                    var other = hits[i*256+k];
                    matched |= scalar[j].Entity.Equals(other.Entity) && scalar[j].ComponentId == other.ComponentId &&
                        scalar[j].ShapeId == other.ShapeId && scalar[j].LayerId == other.LayerId &&
                        scalar[j].HasLocation == other.HasLocation && MathF.Abs(scalar[j].Distance - other.Distance) < .001f;
                }

                metadata &= matched;
            }

            MotionCheck(answer.Succeeded && results[i].Succeeded && answer.Written == (i % 2 == 0 ? 4 : 128) &&
                answer.Written == results[i].Written && answer.RequiredCapacity == results[i].RequiredCapacity &&
                !answer.Truncated && !results[i].Truncated && ids.Count == answer.Written && metadata &&
                ids.SetEquals(groupedIds), "moving ray/overlap scalar-batch parity");
        }

        ++_queryMotionStages;
    }
}
