namespace CreatorEngine.Scripts;

public sealed partial class PhysicsSensorTransitionProbe : Component
{
    private ContactStream? _pairs, _targets;
    private PhysicsBodyComponent? _body;
    private readonly ulong[] _identities = new ulong[3];
    private readonly int[] _sensorBegins = new int[3], _sensorPersists = new int[3], _sensorEnds = new int[3];
    private int _solidBegin, _solidPersist, _solidEnd, _targetBegin, _targetPersist, _targetEnd;
    private int _targetSolidBegin, _targetSolidEnd, _stage;
    private ulong _tick, _persistTick;
    private Entity _other;
    private ulong _otherComponent;
    private float _elapsed, _settledAt;
    private bool _finished;

    public override void OnBeginSimulation()
    {
        _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing transition body.");
        CheckFlags(true);
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
                if (!contact.SelfEntity.Equals(Entity) || contact.SelfComponentId != _body!.ComponentId ||
                    contact.OtherShapeId != 23 || (contact.SelfShapeId != 19 && contact.SelfShapeId != 20) ||
                    contact.Tick == 0 || contact.Tick < _tick)
                    throw new InvalidOperationException("Transition endpoint/tick mismatch.");
                _tick = contact.Tick;
                if (!_other.IsAlive)
                {
                    _other = contact.OtherEntity;
                    _otherComponent = contact.OtherComponentId;
                }
                if (!contact.OtherEntity.Equals(_other) || contact.OtherComponentId != _otherComponent)
                    throw new InvalidOperationException("Transition target changed.");

                ulong identity = ((ulong)contact.SelfBodySlot << 32) | contact.SelfBodyGeneration;
                int index = Array.IndexOf(_identities, identity);
                if (index < 0)
                {
                    if (_stage > 2 || _identities[_stage] != 0)
                        throw new InvalidOperationException("Unexpected transition body.");
                    index = _stage;
                    _identities[index] = identity;
                }
                if (contact.Phase != ContactPhases.End && index != _stage)
                    throw new InvalidOperationException("Retired transition body emitted stale contact.");
                if (contact.Sensor != (index != 1 || contact.SelfShapeId == 20))
                    throw new InvalidOperationException("Sensor/solid snapshot flag mismatch.");

                if (contact.Sensor)
                {
                    if (contact.Phase == ContactPhases.Begin) ++_sensorBegins[index];
                    else if (contact.Phase == ContactPhases.Persist) ++_sensorPersists[index];
                    else ++_sensorEnds[index];
                }
                else
                {
                    if (contact.Phase == ContactPhases.Begin) ++_solidBegin;
                    else if (contact.Phase == ContactPhases.Persist) ++_solidPersist;
                    else ++_solidEnd;
                }
            }
            foreach (ref readonly var contact in _targets!.Read())
            {
                if (!contact.OtherEntity.Equals(_other)) throw new InvalidOperationException("Grouped target changed.");
                if (!contact.Sensor)
                {
                    if (contact.Phase == ContactPhases.Begin) ++_targetSolidBegin;
                    else if (contact.Phase == ContactPhases.End) ++_targetSolidEnd;
                    continue;
                }
                if (contact.Phase == ContactPhases.Begin) ++_targetBegin;
                else if (contact.Phase == ContactPhases.End) ++_targetEnd;
                else
                {
                    if (contact.Tick <= _persistTick) throw new InvalidOperationException("Duplicate target Persist.");
                    _persistTick = contact.Tick;
                    ++_targetPersist;
                }
            }
            if (_stage == 0 && _sensorBegins[0] == 2 && _sensorPersists[0] >= 2)
            {
                Replace(false);
                _stage = 1;
            }
            else if (_stage == 1 && _sensorEnds[0] == 2 && _sensorBegins[1] == 1 &&
                _sensorPersists[1] > 0 && _solidBegin == 1 && _solidPersist > 0)
            {
                Replace(true);
                _stage = 2;
            }
            else if (_stage == 2 && _sensorEnds[1] == 1 && _solidEnd == 1 && _sensorBegins[2] == 2 && _sensorPersists[2] >= 2)
            {
                Transform.WorldPosition = new Float3(10, 0, 0);
                Require(_body!.SetVelocity(default));
                _stage = 3;
            }
            else if (_stage == 3 && _sensorEnds[2] == 2)
            {
                _stage = 4;
                _settledAt = _elapsed;
            }
            else if (_stage == 4 && _elapsed - _settledAt > .1f)
            {
                if (_sensorBegins[0] != 2 || _sensorBegins[1] != 1 || _sensorBegins[2] != 2 ||
                    _sensorEnds[0] != 2 || _sensorEnds[1] != 1 || _sensorEnds[2] != 2 ||
                    _solidBegin != 1 || _solidEnd != 1 || _targetBegin < 1 || _targetBegin > 3 ||
                    _targetEnd != _targetBegin || _targetSolidBegin != 1 || _targetSolidEnd != 1 ||
                    _identities[0] == _identities[1] || _identities[1] == _identities[2] || _identities[0] == _identities[2])
                    throw new InvalidOperationException("Transition counts or identity acceptance failed.");
                _finished = true;
                Print(true, "");
            }
            if (_elapsed > 15) throw new InvalidOperationException("Sensor transition timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void Replace(bool sensor)
    {
        Require(_body!.SetShapeFlags(19, sensor, true));
        CheckFlags(sensor);
    }

    private void CheckFlags(bool sensor)
    {
        if (_body!.GetShape(1, out var shape) != PhysicsError.None || shape.ShapeId != 19 ||
            shape.Sensor != sensor || !shape.QueryEnabled || shape.ContactRole != ShapeRoles.Attack)
            throw new InvalidOperationException("Transition shape flags/role not applied or restored.");
    }

    private static void Require(PhysicsError error)
    {
        if (error != PhysicsError.None) throw new InvalidOperationException($"Transition operation: {error}");
    }

    private void Print(bool success, string error) => Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
    {
        marker = "physics.contact.stream", owner = Entity.Name, success, error, sensorTransition = true,
        sensorBegins = _sensorBegins, sensorPersists = _sensorPersists, sensorEnds = _sensorEnds,
        solidBegin = _solidBegin, solidPersist = _solidPersist, solidEnd = _solidEnd,
        groupBegin = _targetBegin, groupPersist = _targetPersist, groupEnd = _targetEnd,
        groupSolidBegin = _targetSolidBegin, groupSolidEnd = _targetSolidEnd, bodies = _identities, tick = _tick
    }));
}
