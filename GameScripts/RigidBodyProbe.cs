namespace CreatorEngine.Scripts;

// Class name retained for existing script asset identity; the tested API is PhysicsBodyComponent.
public sealed partial class RigidBodyProbe : Component
{
    private bool _checked;

    public override void PostPhysics(float tick)
    {
        if (_checked) return;
        _checked = true;

        var body = GetComponent<PhysicsBodyComponent>();
        if (body is null) { LogError("[RigidBodyProbe] FAIL required body missing or ambiguous"); return; }

        Check("state", body.ReadState(out var before) == PhysicsError.None);
        Check("shape count", body.GetShapeCount(out var count) == PhysicsError.None && count > 0);
        for (int i = 0; i < count; ++i)
            Check("stable shape identity", body.GetShape(i, out var shape) == PhysicsError.None && shape.ShapeId != 0);

        Check("explicit identity lookup", PhysicsBodyComponent.Find(Entity, body.ComponentId)?.ComponentId == body.ComponentId);
        if (before.Kind != PhysicsBodyKind.Dynamic) return;

        var velocity = new Float3(1, 2, 3);
        Check("velocity update", body.SetVelocity(velocity) == PhysicsError.None);
        Check("velocity round trip", body.ReadState(out var after) == PhysicsError.None &&
            MathF.Abs(after.LinearVelocity.X - velocity.X) < 0.001f &&
            MathF.Abs(after.LinearVelocity.Y - velocity.Y) < 0.001f &&
            MathF.Abs(after.LinearVelocity.Z - velocity.Z) < 0.001f);
        Check("restore velocities", body.SetVelocity(before.LinearVelocity, before.AngularVelocity) == PhysicsError.None);
    }

    private void Check(string name, bool passed)
    {
        if (passed) Log($"[RigidBodyProbe] PASS {name}");
        else LogError($"[RigidBodyProbe] FAIL {name}");
    }
}
