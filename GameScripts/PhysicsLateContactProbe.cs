namespace CreatorEngine.Scripts;

// Product gate: created after a sensor has entered and persisted. Its owner has
// no physics body; the subscriptions select the already active attack owner.
public sealed partial class PhysicsLateContactProbe : Component
{
    private ContactStream? _all, _beginOnly;
    private int _begins, _persists, _beginOnlyCount;
    private ulong _firstTick, _lastTick;
    private bool _finished;

    public override void OnBeginSimulation()
    {
        if (!PhysicsContactStreamProbe.AttackOwner.IsAlive || PhysicsContactStreamProbe.ObserverSpawnTick == 0)
            throw new InvalidOperationException("Observer started before active sensor pair.");

        _all = Physics.ObserveContacts(PhysicsContactStreamProbe.AttackOwner, ShapeRoles.Attack,
            ShapeRoles.Hurt, ContactPhases.All, 16);
        _beginOnly = Physics.ObserveContacts(PhysicsContactStreamProbe.AttackOwner, ShapeRoles.Attack,
            ShapeRoles.Hurt, ContactPhases.Begin, 16);
        PhysicsContactStreamProbe.Track(_all);
        PhysicsContactStreamProbe.Track(_beginOnly);
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;
        try
        {
            foreach (ref readonly var contact in _all!.Read())
            {
                if (!contact.Sensor || contact.SelfShapeId != 19 || contact.OtherShapeId != 23 ||
                    !contact.SelfEntity.Equals(PhysicsContactStreamProbe.AttackOwner) ||
                    contact.OtherEntity.Name != "ContactHurt" || contact.Tick <= PhysicsContactStreamProbe.ObserverSpawnTick ||
                    contact.Tick < _lastTick)
                    throw new InvalidOperationException("Late overlap endpoint/tick mismatch.");

                if (_firstTick == 0) _firstTick = contact.Tick;
                _lastTick = contact.Tick;
                if (contact.Phase == ContactPhases.Begin) ++_begins;
                else if (contact.Phase == ContactPhases.Persist) ++_persists;
                else throw new InvalidOperationException("Sensor ended before observer acceptance.");
            }
            foreach (ref readonly var contact in _beginOnly!.Read())
            {
                if (!contact.Sensor || contact.Phase != ContactPhases.Begin || contact.Tick != _firstTick)
                    throw new InvalidOperationException("Begin-only late overlap mismatch.");
                ++_beginOnlyCount;
            }
            if (_begins > 1 || _beginOnlyCount > 1)
                throw new InvalidOperationException("Late overlap Begin replayed.");

            if (_persists >= 2)
            {
                if (_begins != 1 || _beginOnlyCount != 1)
                    throw new InvalidOperationException("Late overlap initial Begin missing.");
                _finished = PhysicsContactStreamProbe.LateReady = true;
                Print(true, "");
            }
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void Print(bool success, string error) => Console.WriteLine(
        System.Text.Json.JsonSerializer.Serialize(new { marker = "physics.contact.late", success, error,
            spawnTick = PhysicsContactStreamProbe.ObserverSpawnTick, firstTick = _firstTick, lastTick = _lastTick,
            begins = _begins, persists = _persists, beginOnly = _beginOnlyCount }));
}
