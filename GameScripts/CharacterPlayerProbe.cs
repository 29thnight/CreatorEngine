namespace CreatorEngine.Scripts;

// Runs inside the packaged Scene on real completed physics ticks.
public sealed partial class CharacterPlayerProbe : Component
{
    private CharacterMovementComponent? _character;
    private int _stage, _passed;
    private ulong _lastTick;
    private float _forceStart, _movementStart;
    private bool _finished, _transferred, _ddolFinished;
    private int _additions, _removals;

    private static CharacterMovementComponent? _sourceCharacter;
    private static PhysicsBodyComponent? _sourceBody;
    private static Entity _sourceActor, _sourceFloor;
    private static uint _sourceShape;

    // Delivered through the generated message dispatcher after rejected activation.
    public void VerifyFailedActivationWrappers()
    {
        if (Environment.GetEnvironmentVariable("CE_PHYSICS_WRAPPER_PROBE") != "1") return;

        int passed = 0;
        void Require(bool valid, string label)
        {
            if (!valid) throw new InvalidOperationException(label);
            ++passed;
        }

        bool retained = Environment.GetEnvironmentVariable("CE_PHYSICS_WRAPPER_RETAINED") == "1";
        try
        {
            Require(_sourceCharacter is not null && _sourceBody is not null, "source capture");

            Require(!_sourceFloor.IsAlive, "source floor retired");
            Require(_sourceBody!.ReadState(out var body) == PhysicsError.StaleHandle && body.Mass == 0, "body read");
            Require(_sourceBody.SetVelocity(new Float3(9, 0, 0)) == PhysicsError.StaleHandle, "body velocity");
            Require(_sourceBody.ApplyForce(new Float3(9, 0, 0)) == PhysicsError.StaleHandle, "body force");
            Require(_sourceBody.GetShapeCount(out int count) == PhysicsError.StaleHandle && count == 0, "shape count");
            Require(_sourceBody.GetShape(0, out var shape) == PhysicsError.StaleHandle && shape.ShapeId == 0, "shape read");
            Require(_sourceBody.SetShapeFlags(_sourceShape, true, false) == PhysicsError.StaleHandle, "shape flags");

            Require(_sourceActor.IsAlive == retained, "source actor lifetime");
            PhysicsError read = retained ? PhysicsError.None : PhysicsError.StaleHandle;
            PhysicsError command = retained ? PhysicsError.WrongPhase : PhysicsError.StaleHandle;
            Require(_sourceCharacter!.ReadState(out var before) == read && !before.Simulating &&
                    (retained || before.Tick == 0), "character read");
            Require(_sourceCharacter.SetDesiredVelocity(new Float3(9, 0, 0)) == command, "character velocity");
            Require(_sourceCharacter.Jump() == command, "character jump");
            Require(_sourceCharacter.ForceVelocity(new Float3(9, 0, 0), 1) == command, "character force");
            Require(_sourceCharacter.CancelForcedVelocity() == command, "character cancel");
            Require(_sourceCharacter.Teleport(new Float3(99, 99, 99)) == command, "character teleport");
            Require(_sourceCharacter.ReadState(out var after) == read && after.Tick == before.Tick &&
                    after.Position.X == before.Position.X && after.Position.Y == before.Position.Y &&
                    after.Position.Z == before.Position.Z && after.DesiredVelocity.X == before.DesiredVelocity.X &&
                    after.ForcedRemaining == before.ForcedRemaining && after.Flags == before.Flags, "rejected commands preserve state");

            Console.WriteLine($"[physics.player.wrappers] {{\"passed\":{passed},\"failed\":0,\"retained\":{retained.ToString().ToLowerInvariant()},\"complete\":true}}");
        }
        catch (Exception error)
        {
            Console.WriteLine($"[physics.player.wrappers] {{\"passed\":{passed},\"failed\":1,\"complete\":false}}");
            Console.WriteLine($"[physics.player.wrappers.failure] {error.Message}");
        }
    }

    private string ProbeLabel => Entity.Name switch
    {
        "ConvexGateActor" => "physics.player.convex",
        "HeightfieldGateActor" => "physics.player.heightfield",
        _ => "physics.player"
    };

    public override void OnAddedToScene()
    {
        ++_additions;
        if (_character is not null)
        {
            _transferred = true;
            _lastTick = 0;
        }
    }

    public override void OnRemovingFromScene() => ++_removals;

    private void VerifyTransfer()
    {
        if (_ddolFinished) return;

        try
        {
            if (_character is null || _character.ReadState(out var state) != PhysicsError.None)
                throw new InvalidOperationException("retained wrapper read");

            if (state.Tick < 30) return;

            Console.WriteLine(FormattableString.Invariant(
                $"[physics.player.ddol.state] positionY={state.Position.Y} footY={state.FootPosition.Y} below={state.Below} fall={state.FallVelocity} tick={state.Tick}"));

            _passed = 0;
            Check(_additions == 2 && _removals == 1, "balanced transfer lifecycle");
            Check(state.Simulating, "destination simulation");
            Check(state.DesiredVelocity.X == 1.25f, "retained desired input");
            Check(state.Forced && state.ForcedRemaining > 9 && state.ForcedRemaining <= 9.51, "retained force duration");
            Check(state.Position.X > 1.4f && state.Position.X < 1.8f, "continued forced movement");
            // Hierarchy transfer retains a scaled capsule in a populated destination.
            // Check downward progress; its collisions need not match the isolated capsule trajectory.
            bool hierarchy = Environment.GetEnvironmentVariable("CE_PHYSICS_DDOL_HIERARCHY") == "1";
            Check(hierarchy
                ? state.Position.Y < 4.99f && state.FootPosition.Y > 0
                : state.Position.Y > 3.3f && state.Position.Y < 4.2f, "continued gravity");
            Check(state.FallVelocity < 0, "fall state");
            Check(state.Tick >= 30, "destination fixed ticks");
            _ddolFinished = true;
            Console.WriteLine($"[physics.player.ddol] {{\"passed\":{_passed},\"failed\":0,\"tick\":{state.Tick},\"complete\":true}}");
        }
        catch (Exception error)
        {
            _ddolFinished = true;
            Console.WriteLine($"[physics.player.ddol] {{\"passed\":{_passed},\"failed\":1,\"complete\":false}}");
            Console.WriteLine($"[physics.player.ddol.failure] {error.Message}");
        }
    }

    private void Check(bool valid, string name)
    {
        if (!valid) throw new InvalidOperationException(name);
        ++_passed;
    }

    public override void PostPhysics(float tick)
    {
        if (_transferred)
        {
            VerifyTransfer();
            return;
        }

        if (_finished) return;
        try
        {
            _character ??= GetComponent<CharacterMovementComponent>();
            if (_character is null) throw new InvalidOperationException("character lookup");
            if (_character.ReadState(out var state) != PhysicsError.None || !state.Simulating)
                throw new InvalidOperationException("active state");
            if (state.Tick == 0 || state.Tick == _lastTick) return;
            _lastTick = state.Tick;

            if (_sourceCharacter is null && Entity.Name == "CharacterGateActor" &&
                Environment.GetEnvironmentVariable("CE_PHYSICS_WRAPPER_PROBE") == "1")
            {
                _sourceActor = Entity;
                _sourceFloor = Entity.Find("CharacterGateFloor");
                _sourceBody = _sourceFloor.GetComponent<PhysicsBodyComponent>();
                if (_sourceBody is null || _sourceBody.GetShape(0, out var shape) != PhysicsError.None)
                    throw new InvalidOperationException("source body capture");

                _sourceShape = shape.ShapeId;
                _sourceCharacter = _character;
            }

            switch (_stage)
            {
                case 0 when state.Below:
                    Check(state.Tick > 0, "completed fixed step");
                    Check(Math.Abs(state.FootPosition.Y) < .08f, "cooked floor collision");
                    Check(_character.SetDesiredVelocity(new Float3(1, 0, 0)) == PhysicsError.None, "velocity");
                    _movementStart = state.Position.X;
                    _stage = 1;
                    break;
                case 1 when state.Position.X - _movementStart > .4f:
                    Check(state.Below && state.MovementVelocity.X > .9f, "accelerated grounded movement");
                    Check(_character.Jump() == PhysicsError.None, "jump");
                    _stage = 2;
                    break;
                case 2 when state.FootPosition.Y > .3f:
                    Check(!state.Below && state.FallVelocity > 0, "jump rise");
                    Check(_character.Jump() == PhysicsError.WrongPhase, "airborne jump rejection");
                    _stage = 3;
                    break;
                case 3 when state.Below:
                    Check(Math.Abs(state.FootPosition.Y) < .08f, "landing");
                    _forceStart = state.Position.X;
                    Check(_character.ForceVelocity(new Float3(3, 0, 0), .15) == PhysicsError.None, "force");
                    _stage = 4;
                    break;
                case 4 when !state.Forced:
                    Check(state.ForcedRemaining == 0 && state.Position.X - _forceStart >= .4f, "force expiration");
                    Check(_character.Teleport(new Float3(0, 5, 0)) == PhysicsError.None, "teleport");
                    Check(_character.ReadState(out var moved) == PhysicsError.None && moved.Position.Y == 5 &&
                          moved.FallVelocity == 0 && !moved.Below && moved.MovementVelocity.X == 0, "teleport reset");
                    _finished = true;
                    Console.WriteLine($"[{ProbeLabel}] {{\"passed\":{_passed},\"failed\":0,\"tick\":{state.Tick},\"complete\":true}}");
                    break;
            }
        }
        catch (Exception error)
        {
            _finished = true;
            Console.WriteLine($"[{ProbeLabel}] {{\"passed\":{_passed},\"failed\":1,\"complete\":false}}");
            Console.WriteLine($"[physics.player.failure] {error.Message}");
        }
    }
}
