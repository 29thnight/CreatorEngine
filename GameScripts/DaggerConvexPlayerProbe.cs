namespace CreatorEngine.Scripts;

public sealed partial class DaggerConvexPlayerProbe : Component
{
    private PhysicsBodyComponent? _body;
    private int _tick, _passed, _phase;
    private float _elapsed, _launchTime, _impulseTime;
    private bool _finished;
    private float _startX, _beforeImpulse;
    private Quaternion _startRotation;

    private void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
        ++_passed;
    }

    private static float Speed(Float3 value) => MathF.Sqrt(value.X*value.X + value.Y*value.Y + value.Z*value.Z);

    public override void PostPhysics(float delta)
    {
        if (_finished) return;
        try
        {
            _body ??= GetComponent<PhysicsBodyComponent>();
            if (_body is null || _body.ReadState(out var state) != PhysicsError.None)
                throw new InvalidOperationException("dynamic body lookup/read");

            ++_tick;
            _elapsed += delta;
            bool sample = false;
            CheckFinite(state);
            if (_phase == 0)
            {
                Check(state.Kind == PhysicsBodyKind.Dynamic, "dynamic kind");
                Check(MathF.Abs(state.Mass-1) < .001f, "one kilogram mass");
                Check(_body.GetShapeCount(out var count) == PhysicsError.None && count == 1 &&
                    _body.GetShape(0, out var shape) == PhysicsError.None && shape.Kind == PhysicsShapeKind.Convex, "single real convex");
                _phase = 1;
                sample = true;
            }
            if (_phase == 1 && _elapsed >= .5f)
            {
                Check(state.Position.Y < 2.5f && state.LinearVelocity.Y < -1, "gravity fall");
                _phase = 2;
                sample = true;
            }

            if (_phase == 2 && _elapsed >= 4f)
            {
                Check(state.Position.Y > -.05f && state.Position.Y < .4f, "floor contact prevents tunneling");
                Check(Speed(state.LinearVelocity) < .15f, "contact settling");
                _startX = state.Position.X;
                _startRotation = state.Rotation;
                Check(_body.SetVelocity(new Float3(1, 2, 0), new Float3(0, 0, 2)) == PhysicsError.None, "linear/angular command");
                _launchTime = _elapsed;
                _phase = 3;
                sample = true;
            }
            if (_phase == 3 && _elapsed >= _launchTime + .2f)
            {
                Check(state.Position.X > _startX+.1f, "linear movement");
                Check(state.Position.Y > .15f, "vertical launch");
                float rotationDot = MathF.Abs(state.Rotation.X*_startRotation.X + state.Rotation.Y*_startRotation.Y +
                    state.Rotation.Z*_startRotation.Z + state.Rotation.W*_startRotation.W);
                Check(Speed(state.AngularVelocity) > .5f && rotationDot < .999f, "angular movement and pose rotation");
                _beforeImpulse = state.LinearVelocity.X;
                Check(_body.ApplyForce(new Float3(2,0,0), mode: PhysicsForceMode.Impulse) == PhysicsError.None, "impulse submission");
                _impulseTime = _elapsed;
                _phase = 4;
                sample = true;
            }
            if (_phase == 4 && _elapsed >= _impulseTime + .03f)
            {
                Check(state.LinearVelocity.X > _beforeImpulse+1.5f, "impulse velocity response");
                _phase = 5;
                sample = true;
            }

            if (_phase == 5 && _elapsed >= 10f)
            {
                Check(state.Position.Y > -.05f && state.Position.Y < .5f && Speed(state.LinearVelocity) < .2f, "second landing/settling");
                var visual = Transform.WorldPosition;
                float dx = visual.X-state.Position.X, dy = visual.Y-state.Position.Y, dz = visual.Z-state.Position.Z;
                Check(dx*dx+dy*dy+dz*dz < 1e-6f, "render entity follows physics pose");
                _finished = true;
                sample = true;
                Console.WriteLine($"[physics.player.dagger] {{\"passed\":{_passed},\"failed\":0,\"tick\":{_tick},\"height\":{state.Position.Y},\"speed\":{Speed(state.LinearVelocity)},\"complete\":true}}");
            }
            if (sample)
                Console.WriteLine($"[physics.player.dagger.sample] {{\"tick\":{_tick},\"seconds\":{_elapsed},\"x\":{state.Position.X},\"y\":{state.Position.Y},\"vx\":{state.LinearVelocity.X},\"vy\":{state.LinearVelocity.Y},\"angularSpeed\":{Speed(state.AngularVelocity)}}}");
        }
        catch (Exception error)
        {
            _finished = true;
            Console.WriteLine($"[physics.player.dagger] {{\"passed\":{_passed},\"failed\":1,\"tick\":{_tick},\"complete\":false}}");
            Console.WriteLine($"[physics.player.dagger.failure] {error.Message}");
        }
    }

    private static void CheckFinite(PhysicsBodyState state)
    {
        if (!float.IsFinite(state.Position.X) || !float.IsFinite(state.Position.Y) || !float.IsFinite(state.Position.Z) ||
            !float.IsFinite(Speed(state.LinearVelocity)) || !float.IsFinite(Speed(state.AngularVelocity)))
            throw new InvalidOperationException("nonfinite simulation state");
    }
}
