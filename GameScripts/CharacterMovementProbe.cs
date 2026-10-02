namespace CreatorEngine.Scripts;

public sealed partial class CharacterMovementProbe : Component
{
    private bool _checked;
    private static CharacterMovementComponent? _captured;
    private static int _completed, _passed, _failed;

    [EngineCallable]
    public static string Results() => FormattableString.Invariant(
        $"{{\"completed\":{_completed},\"passed\":{_passed},\"failed\":{_failed}}}");

    [EngineCallable]
    public static string Retired()
    {
        var stale = _captured is not null && _captured.ReadState(out var state) == PhysicsError.StaleHandle &&
            state.Tick == 0 && _captured.SetDesiredVelocity(default) == PhysicsError.StaleHandle &&
            _captured.Teleport(default) == PhysicsError.StaleHandle && _captured.Jump() == PhysicsError.StaleHandle &&
            _captured.ForceVelocity(default, 1) == PhysicsError.StaleHandle && _captured.CancelForcedVelocity() == PhysicsError.StaleHandle;
        return stale ? "retired" : "failure";
    }

    [EngineCallable]
    public static string Transferred()
    {
        // The script registry follows Entity lifetime; only the scene-scoped CLI handle expires.
        var alive = _captured is not null && _captured.ReadState(out var state) == PhysicsError.None &&
            state.Simulating && state.DesiredVelocity.X == 1.25f && state.Forced && state.ForcedRemaining > 0;
        return alive ? "transferred" : "failure";
    }

    [EngineCallable]
    public static string JumpGrounded() => _captured is null ? "missing" : _captured.Jump().ToString();

    public override void PostPhysics(float tick)
    {
        if (_checked) return;
        var character = GetComponent<CharacterMovementComponent>();
        if (character is null) { _checked = true; Check("lookup", false); ++_completed; return; }
        // A render frame can complete before the accumulator produces its first fixed step.
        if (character.ReadState(out var pending) == PhysicsError.None && pending.Tick == 0) return;
        _checked = true;
        Check("lookup", true);

        _captured = character;

        Check("state", character.ReadState(out var before) == PhysicsError.None && before.Simulating && before.Tick > 0);
        Check("identity", CharacterMovementComponent.Find(Entity, character.ComponentId)?.ComponentId == character.ComponentId);
        Check("wrong identity", CharacterMovementComponent.Find(Entity, ulong.MaxValue) is null);
        Check("velocity", character.SetDesiredVelocity(new Float3(1, 0, 0)) == PhysicsError.None);
        Check("input round trip", character.ReadState(out var input) == PhysicsError.None && input.DesiredVelocity.X == 1);
        Check("NaN rejected", character.SetDesiredVelocity(new Float3(float.NaN, 0, 0)) == PhysicsError.InvalidArgument);
        Check("invalid input unchanged", character.ReadState(out input) == PhysicsError.None && input.DesiredVelocity.X == 1);
        Check("teleport", character.Teleport(new Float3(0, 3, 0)) == PhysicsError.None);
        Check("teleport state", character.ReadState(out var moved) == PhysicsError.None && moved.Position.Y == 3 &&
            moved.FallVelocity == 0 && !moved.Below && !moved.Above && !moved.Sides && moved.DesiredVelocity.X == 1);
        Check("airborne jump rejected", character.Jump() == PhysicsError.WrongPhase);
        Check("forced velocity", character.ForceVelocity(new Float3(2, 0, 0), .5) == PhysicsError.None);
        Check("force state", character.ReadState(out var forced) == PhysicsError.None && forced.Forced && forced.ForcedRemaining == .5);
        Check("invalid force", character.ForceVelocity(default, -1) == PhysicsError.InvalidArgument);
        Check("cancel force", character.CancelForcedVelocity() == PhysicsError.None);
        Check("cancel state", character.ReadState(out forced) == PhysicsError.None && !forced.Forced && forced.ForcedRemaining == 0);
        Check("foreign thread", Task.Run(() => character.ReadState(out _)).GetAwaiter().GetResult() == PhysicsError.WrongPhase);
        try { ((Component)character).Enabled = false; Check("activation boundary", false); }
        catch (NotSupportedException) { Check("activation boundary", true); }
        ++_completed;
    }

    private void Check(string name, bool passed)
    {
        if (passed) { ++_passed; Log($"[CharacterMovementProbe] PASS {name}"); }
        else { ++_failed; LogError($"[CharacterMovementProbe] FAIL {name}"); }
    }
}
