namespace CreatorEngine.Scripts;

public sealed partial class PhysicsB2PlayerProbe
{
    private int _mixedPhase, _mixedPassed;
    private float _mixedTime;
    private PhysicsBodyComponent? _mixedDynamic, _mixedKinematic;
    private Entity _mixedOwner;

    private void MixedCheck(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
        ++_mixedPassed;
    }

    private void MixedQuery(Float3 point, ulong expected, bool present)
    {
        Span<PhysicsHit> scalar = stackalloc PhysicsHit[8];
        Span<PhysicsHit> batch = stackalloc PhysicsHit[8];
        Span<PhysicsQueryRequest> requests = stackalloc PhysicsQueryRequest[1];
        Span<PhysicsBatchResult> results = stackalloc PhysicsBatchResult[1];
        requests[0] = PhysicsQueryRequest.OverlapSphere(point, .1f, 0, 8);
        var answer = Physics.OverlapSphere(Entity, point, .1f, scalar);
        MixedCheck(Physics.QueryBatch(Entity, requests, batch, results) == PhysicsError.None &&
            answer.Succeeded && results[0].Succeeded && !answer.Truncated && !results[0].Truncated &&
            answer.Written == results[0].Written, "mixed scalar/batch counts");

        bool found = false, parity = true;
        for (int i = 0; i < answer.Written; ++i)
        {
            found |= scalar[i].ComponentId == expected;
            bool match = false;
            for (int j = 0; j < results[0].Written; ++j)
                match |= scalar[i].ComponentId == batch[j].ComponentId &&
                    scalar[i].Entity.Equals(batch[j].Entity) && scalar[i].ShapeId == batch[j].ShapeId;
            parity &= match;
        }

        MixedCheck(parity && found == present, "mixed query identity/presence");
    }

    private void RunMixedQueries(float delta)
    {
        if (!Dynamic || _mixedPhase == 4 || Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_MIXED") != "1") return;
        var gate = Environment.GetEnvironmentVariable("CE_PHYSICS_QUERY_GATE");
        if (!string.IsNullOrEmpty(gate) && !System.IO.File.Exists(gate)) return;

        try
        {
            _mixedTime += delta;
            if (_mixedTime > 8) throw new InvalidOperationException("mixed query timeout");
            if (_mixedPhase == 0)
            {
                _mixedDynamic = Entity.Find("MixedDynamic").GetComponent<PhysicsBodyComponent>();
                _mixedOwner = Entity.Find("MixedKinematic");
                _mixedKinematic = _mixedOwner.GetComponent<PhysicsBodyComponent>();
                MixedCheck(_mixedDynamic is not null && _mixedKinematic is not null, "mixed body lookup");
                MixedQuery(new Float3(110, 20, 1.5f), _mixedKinematic!.ComponentId, false);
                _mixedOwner.Transform!.WorldRotation = new Quaternion(0, .70710678f, 0, .70710678f);
                MixedCheck(_mixedDynamic!.SetVelocity(new Float3(10, 0, 0)) == PhysicsError.None, "collision velocity");
                _mixedPhase = 1;
                return;
            }

            MixedCheck(_mixedDynamic!.ReadState(out var moving) == PhysicsError.None &&
                _mixedKinematic!.ReadState(out var target) == PhysicsError.None, "mixed completed states");
            if (_mixedPhase == 1 && _mixedTime > .15f)
            {
                MixedQuery(new Float3(110, 20, 1.5f), _mixedKinematic!.ComponentId, true);
                MixedQuery(new Float3(111.5f, 20, 0), _mixedKinematic.ComponentId, false);
                _mixedOwner.Transform!.WorldPosition = new Float3(115, 20, 0);
                _mixedPhase = 2;
            }
            else if (_mixedPhase == 2 && _mixedTime > .35f)
            {
                MixedQuery(new Float3(110, 20, 1.5f), _mixedKinematic!.ComponentId, false);
                MixedQuery(new Float3(115, 20, 1.5f), _mixedKinematic.ComponentId, true);
                _mixedPhase = 3;
            }
            else if (_mixedPhase == 3 && _mixedTime > 1)
            {
                MixedCheck(moving.Position.X > 103 && moving.Position.X < 104.2f &&
                    MathF.Abs(moving.LinearVelocity.X) < .1f, "dynamic collision stops at static wall");
                MixedQuery(moving.Position, _mixedDynamic.ComponentId, true);
                MixedQuery(new Float3(100, 20, 0), _mixedDynamic.ComponentId, false);
                _mixedPhase = 4;
                Console.WriteLine($"[physics.player.mixed-query] {{\"passed\":{_mixedPassed},\"failed\":0,\"collisionX\":{moving.Position.X},\"complete\":true}}");
            }
        }
        catch (Exception error)
        {
            _mixedPhase = 4;
            Console.WriteLine($"[physics.player.mixed-query.failure] {error.Message}");
        }
    }
}
