namespace CreatorEngine.Scripts;

// Product gate: observes completed fixed steps, rather than render-frame timing.
public sealed partial class CharacterPlanarInputProbe : Component
{
    private CharacterMovementComponent? _character;
    private ulong _startTick, _lastTick;
    private Float3 _start;
    private int _stage, _passed;
    private bool _finished;
    private static CharacterMovementComponent? _retired;

    public override void OnEndSimulation() => _retired = _character;

    [EngineCallable]
    public static string Retired()
    {
        if (_retired is null) return "missing";

        return _retired.ReadState(out var state) == PhysicsError.StaleHandle && state.Tick == 0 &&
               _retired.SetPlanarInput(Float3.Right, 4) == PhysicsError.StaleHandle
            ? "retired" : "retargeted";
    }

    public override void OnBeginSimulation()
    {
        _character = GetComponent<CharacterMovementComponent>();
        if (_character is null) throw new InvalidOperationException("character binding");

        Transform.SetWorldRotation(Quaternion.LookRotation(Float3.Forward));
        Require(_character.SetPlanarInput(new Float3(.25f, 7, 0), 4) == PhysicsError.None, "analog request");
    }

    private void Require(bool valid, string name)
    {
        if (!valid) throw new InvalidOperationException(name);

        ++_passed;
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;

        try
        {
            if (_character is null || _character.ReadState(out var state) != PhysicsError.None || !state.Simulating)
                throw new InvalidOperationException("active character");

            if (state.Tick == 0 || state.Tick == _lastTick) return;

            _lastTick = state.Tick;
            if (_startTick == 0) { _startTick = state.Tick; _start = state.Position; }
            if (state.Tick - _startTick < 60) return;

            switch (_stage)
            {
                case 0:
                    Require(state.DesiredVelocity.X == 1 && state.DesiredVelocity.Y == 0, "analog desired velocity");
                    Require(state.Position.X > _start.X + .7f && MathF.Abs(state.Position.Z - _start.Z) < .02f, "analog physical movement");
                    Require(Float3.Dot(Transform.Forward, Float3.Forward) > .999f, "strafe preserves facing");
                    Require(_character.SetPlanarInput(new Float3(1, 0, 1), 4) == PhysicsError.None, "diagonal request");
                    break;

                case 1:
                    Require(MathF.Abs(state.DesiredVelocity.Length - 4) < 1e-4f, "diagonal speed cap");
                    Require(state.Position.X > _start.X + 1 && state.Position.Z > _start.Z + 1, "diagonal physical movement");
                    Transform.SetWorldRotation(Quaternion.LookRotation(Float3.Right));
                    Require(_character.SetPlanarInput(default, 4) == PhysicsError.None, "release request");
                    break;

                case 2:
                    Require(state.DesiredVelocity.LengthSquared == 0 && state.MovementVelocity.Length < .01f, "fixed step braking");
                    Require(Float3.Dot(Transform.Forward, Float3.Right) > .999f, "explicit script facing");
                    Require(_character.SetPlanarInput(new Float3(float.NaN, 0, 0), 4) == PhysicsError.InvalidArgument, "invalid input rejection");
                    Require(_character.ForceVelocity(new Float3(0, 0, 2), 2) == PhysicsError.None, "independent forced movement");
                    break;

                case 3:
                    Require(state.Forced && state.DesiredVelocity.LengthSquared == 0, "force preserves released input");
                    Require(state.Position.Z > _start.Z + 1.8f, "forced physical movement");
                    Require(Float3.Dot(Transform.Forward, Float3.Right) > .999f, "force preserves explicit facing");
                    Require(_character.CancelForcedVelocity() == PhysicsError.None, "force cleanup");
                    _finished = true;
                    Console.WriteLine($"[physics.player.planar] {{\"passed\":{_passed},\"failed\":0,\"tick\":{state.Tick},\"complete\":true}}");
                    break;
            }

            ++_stage;
            _startTick = state.Tick;
            _start = state.Position;
        }
        catch (Exception error)
        {
            _finished = true;
            Console.WriteLine($"[physics.player.planar] {{\"passed\":{_passed},\"failed\":1,\"complete\":false}}");
            Console.WriteLine($"[physics.player.planar.failure] {error.Message}");
        }
    }
}
