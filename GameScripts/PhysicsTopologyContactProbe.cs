namespace CreatorEngine.Scripts;

// Replace an overlapping compound body through the public runtime API, then exit.
public sealed partial class PhysicsTopologyContactProbe : Component
{
    [SerializeField] private bool _burstReplacements;
    private int _replacementCalls;
    private ContactStream? _pairs, _targets;
    private PhysicsBodyComponent? _body;
    private Entity _other;
    private ulong _otherComponent, _tick, _targetTick;
    private ulong _oldBody, _newBody;
    private int _begins, _persists, _ends, _targetBegins, _targetPersists, _targetEnds, _stage;
    private int _oldEnds, _newBegins, _newEnds;
    private float _elapsed, _finishedAt;
    private bool _finished;

    public override void OnBeginSimulation()
    {
        _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing topology body.");
        if (_body.GetShape(1, out var original) != PhysicsError.None || original.ShapeId != 19 ||
            !original.QueryEnabled || !original.Sensor || original.ContactRole != ShapeRoles.Attack)
            throw new InvalidOperationException("Authored shape flags were not restored before Play.");

        _pairs = Physics.ObserveContacts(Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 64);
        _targets = Physics.ObserveContacts(Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 64,
            ContactGrouping.SensorTargets);
        PhysicsContactStreamProbe.Track(_pairs);
        PhysicsContactStreamProbe.Track(_targets);
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;
        try
        {
            _elapsed += dt;
            foreach (ref readonly var contact in _pairs!.Read())
            {
                if (!contact.Sensor || !contact.SelfEntity.Equals(Entity) || contact.OtherShapeId != 23 ||
                    (contact.SelfShapeId != 19 && contact.SelfShapeId != 20) ||
                    contact.SelfComponentId != _body!.ComponentId || contact.Tick == 0 || contact.Tick < _tick)
                    throw new InvalidOperationException("Topology endpoint/tick mismatch.");

                _tick = contact.Tick;
                if (_oldBody == 0)
                {
                    _oldBody = Identity(contact);
                    _other = contact.OtherEntity;
                    _otherComponent = contact.OtherComponentId;
                }
                if (!contact.OtherEntity.Equals(_other) || contact.OtherComponentId != _otherComponent)
                    throw new InvalidOperationException("Topology target identity changed.");

                if (_stage == 0 && Identity(contact) != _oldBody)
                    throw new InvalidOperationException("Body changed before replacement.");
                if (_stage > 0 && Identity(contact) == _oldBody && contact.Phase != ContactPhases.End)
                    throw new InvalidOperationException("Retired generation produced stale contact.");
                if (Identity(contact) != _oldBody)
                {
                    if (_newBody == 0) _newBody = Identity(contact);
                    if (Identity(contact) != _newBody)
                        throw new InvalidOperationException("Unexpected third body generation.");
                }

                if (contact.Phase == ContactPhases.End && Identity(contact) == _oldBody) ++_oldEnds;
                if (contact.Phase == ContactPhases.Begin && Identity(contact) != _oldBody) ++_newBegins;
                if (contact.Phase == ContactPhases.End && Identity(contact) != _oldBody) ++_newEnds;
                if (_oldEnds > 2 || _newBegins > 2 || _newEnds > 2)
                    throw new InvalidOperationException("Topology generation transition repeated.");

                if (contact.Phase == ContactPhases.Begin) ++_begins;
                else if (contact.Phase == ContactPhases.Persist) ++_persists;
                else ++_ends;
            }
            foreach (ref readonly var contact in _targets!.Read())
            {
                if (!contact.Sensor || !contact.OtherEntity.Equals(_other))
                    throw new InvalidOperationException("Topology target aggregation mismatch.");
                if (contact.Phase == ContactPhases.Begin) ++_targetBegins;
                else if (contact.Phase == ContactPhases.End) ++_targetEnds;
                else
                {
                    if (contact.Tick <= _targetTick) throw new InvalidOperationException("Repeated target Persist.");
                    ++_targetPersists;
                }
                _targetTick = contact.Tick;
            }

            if (_stage == 0 && _begins == 2 && _persists >= 2)
            {
                if (_burstReplacements)
                {
                    // No simulation/fetch between these synchronous public API calls.
                    Replace(19, false, true);
                    Replace(19, true, false);
                    Replace(20, false, true);
                    Replace(20, true, true);
                }
                else Replace(19, true, false);
                if (_body.GetShape(1, out var shape) != PhysicsError.None || shape.QueryEnabled ||
                    !shape.Sensor || shape.ContactRole != ShapeRoles.Attack)
                    throw new InvalidOperationException("Replacement did not preserve role/sensor or apply query flag.");
                _stage = 1;
            }
            else if (_stage == 1 && _begins == 4 && _ends == 2 && _newBody != 0 && _persists >= 4)
            {
                Transform.WorldPosition = new Float3(10, 0, 0);
                Require(_body!.SetVelocity(default));
                _stage = 2;
            }
            else if (_stage == 2 && _ends == 4)
            {
                _stage = 3;
                _finishedAt = _elapsed;
            }
            else if (_stage == 3 && _elapsed - _finishedAt > .1f)
            {
                if (_begins != 4 || _ends != 4 || _targetBegins < 1 || _targetBegins > 2 ||
                    _targetEnds != _targetBegins || _targetPersists < 1 || _oldBody == _newBody ||
                    _oldEnds != 2 || _newBegins != 2 || _newEnds != 2)
                    throw new InvalidOperationException("Topology acceptance incomplete or stale active target.");
                _finished = true;
                Print(true, "");
            }
            if (_elapsed > 15) throw new InvalidOperationException("Topology product timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void Replace(uint shape, bool sensor, bool query)
    {
        Require(_body!.SetShapeFlags(shape, sensor, query));
        ++_replacementCalls;
    }

    private static ulong Identity(in Contact contact) => ((ulong)contact.SelfBodySlot << 32) | contact.SelfBodyGeneration;

    private static void Require(PhysicsError error)
    {
        if (error != PhysicsError.None) throw new InvalidOperationException($"Topology operation: {error}");
    }

    private void Print(bool success, string error) => Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
    {
        marker = "physics.contact.stream", owner = Entity.Name, success, error, topology = true, burst = _burstReplacements, replacementCalls = _replacementCalls,
        sensorBegin = _begins, sensorPersist = _persists, sensorEnd = _ends,
        groupBegin = _targetBegins, groupPersist = _targetPersists, groupEnd = _targetEnds,
        oldEnds = _oldEnds, newBegins = _newBegins, newEnds = _newEnds,
        oldBodyIdentity = _oldBody, newBodyIdentity = _newBody,
        oldSlot = _oldBody >> 32, newSlot = _newBody >> 32,
        oldSlotGeneration = (uint)_oldBody, newSlotGeneration = (uint)_newBody, tick = _tick
    }));
}
