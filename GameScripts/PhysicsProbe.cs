namespace CreatorEngine.Scripts;

// Requires a running physics scene with authored shapes. Missing bodies are failures, not skipped checks.
public sealed partial class PhysicsProbe : Component
{
    private bool _checked;

    public override void PostPhysics(float tick)
    {
        if (_checked) return;
        _checked = true;

        var origin = Transform!.WorldPosition;
        Span<PhysicsHit> hits = stackalloc PhysicsHit[32];
        var overlap = Physics.OverlapSphere(Entity, origin, 50f, hits);
        Check("overlap succeeds", overlap.Succeeded);
        Check("fixture has shapes", overlap.RequiredCapacity > 0);
        Check("written count bounded", overlap.Written <= hits.Length);

        var discovery = Physics.OverlapSphere(Entity, origin, 50f, Span<PhysicsHit>.Empty);
        Check("capacity discovery", discovery.Succeeded && discovery.Written == 0 &&
            discovery.RequiredCapacity == overlap.RequiredCapacity && discovery.Truncated == (discovery.RequiredCapacity > 0));

        Span<PhysicsHit> tiny = stackalloc PhysicsHit[1];
        var small = Physics.OverlapSphere(Entity, origin, 50f, tiny);
        Check("overflow contract", small.Succeeded && small.RequiredCapacity == overlap.RequiredCapacity &&
            small.Written <= tiny.Length && small.Truncated == (small.RequiredCapacity > tiny.Length));

        var ray = Physics.Raycast(Entity, origin + new Float3(0, 5, 0), new Float3(0, -1, 0), 100f, hits);
        Check("ray succeeds", ray.Succeeded);
        for (int i = 0; i < ray.Written; ++i)
            Check("hit identity and location", hits[i].Entity.IsAlive && hits[i].ComponentId != 0 &&
                hits[i].ShapeId != 0 && hits[i].HasLocation && hits[i].Distance >= 0 && hits[i].Distance <= 100);

        var invalid = Physics.OverlapSphere(Entity, origin, -1f, hits);
        Check("invalid query reports error", invalid.Error == PhysicsError.InvalidArgument && invalid.Written == 0);
    }

    private void Check(string name, bool passed)
    {
        if (passed) Log($"[PhysicsProbe] PASS {name}");
        else LogError($"[PhysicsProbe] FAIL {name}");
    }
}
