namespace CreatorEngine.Scripts;

public sealed partial class PhysicsRoleContactProbe : Component
{
    private static readonly ShapeRole Alternate = new(new Guid("c84b601f-9ca1-4ad9-a043-3a235833559b"));
    [SerializeField] private bool _explicitBinding;
    private ContactStream? _attack, _alternate, _attackTargets, _alternateTargets;
    private PhysicsBodyComponent? _body;
    private readonly ulong[] _identities = new ulong[3];
    private readonly int[] _begins = new int[3], _persists = new int[3], _ends = new int[3];
    private int _alternateBegin, _alternatePersist, _alternateEnd, _targetBegin, _targetEnd, _alternateTargetBegin, _alternateTargetEnd;
    private int _stage;
    private bool _finished;
    private float _elapsed, _settledAt;

    public override void OnBeginSimulation()
    {
        _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing role body.");
        CheckRole(ShapeRoles.Attack);
        if (_explicitBinding)
        {
            Require(_body.SetShapeRole(19, default));
            CheckRole(default);
            Physics.BindContactRole(_body, 19, ShapeRoles.Attack);
            bool duplicateRejected = false;
            try { Physics.BindContactRole(_body, 19, ShapeRoles.Attack); }
            catch (InvalidOperationException) { duplicateRejected = true; }
            if (!duplicateRejected) throw new InvalidOperationException("Duplicate scoped binding accepted.");
        }
        _attack = Observe(ShapeRoles.Attack, ContactGrouping.ShapePairs);
        _alternate = Observe(Alternate, ContactGrouping.ShapePairs);
        _attackTargets = Observe(ShapeRoles.Attack, ContactGrouping.SensorTargets);
        _alternateTargets = Observe(Alternate, ContactGrouping.SensorTargets);
    }

    private ContactStream Observe(ShapeRole role, ContactGrouping grouping)
    {
        var stream = Physics.ObserveContacts(Entity, role, ShapeRoles.Hurt, ContactPhases.All, 64, grouping);
        PhysicsContactStreamProbe.Track(stream);
        return stream;
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;

        try
        {
            _elapsed += dt;
            ReadPairs(_attack!, false);
            ReadPairs(_alternate!, true);
            foreach (ref readonly var contact in _attackTargets!.Read())
            {
                if (contact.Phase == ContactPhases.Begin) ++_targetBegin;
                else if (contact.Phase == ContactPhases.End) ++_targetEnd;
            }
            foreach (ref readonly var contact in _alternateTargets!.Read())
            {
                if (contact.Phase == ContactPhases.Begin) ++_alternateTargetBegin;
                else if (contact.Phase == ContactPhases.End) ++_alternateTargetEnd;
            }

            if (_stage == 0 && _begins[0] == 2 && _persists[0] >= 2)
            {
                if (_body!.SetShapeRole(9999, Alternate) != PhysicsError.StaleHandle)
                    throw new InvalidOperationException("Unknown shape role mutation accepted.");
                Require(_body.SetShapeRole(19, Alternate));
                CheckRole(Alternate);
                _stage = 1;
            }
            else if (_stage == 1 && _ends[0] == 2 && _begins[1] == 1 && _persists[1] > 0 &&
                _alternateBegin == 1 && _alternatePersist > 0)
            {
                Require(_body!.SetShapeRole(19, _explicitBinding ? default : ShapeRoles.Attack));
                CheckRole(_explicitBinding ? default : ShapeRoles.Attack);
                _stage = 2;
            }
            else if (_stage == 2 && _ends[1] == 1 && _alternateEnd == 1 && _begins[2] == 2 && _persists[2] >= 2)
            {
                Transform.WorldPosition = new Float3(10, 0, 0);
                Require(_body!.SetVelocity(default));
                _stage = 3;
            }
            else if (_stage == 3 && _ends[2] == 2)
            {
                _stage = 4;
                _settledAt = _elapsed;
            }
            else if (_stage == 4 && _elapsed - _settledAt > .1f)
            {
                if (!_begins.SequenceEqual(new[] {2, 1, 2}) || !_ends.SequenceEqual(new[] {2, 1, 2}) ||
                    _alternateBegin != 1 || _alternateEnd != 1 || _alternateTargetBegin != 1 || _alternateTargetEnd != 1 ||
                    _targetBegin < 1 || _targetBegin > 3 || _targetBegin != _targetEnd || _identities.Distinct().Count() != 3)
                    throw new InvalidOperationException("Role transition acceptance incomplete.");
                // Leave a runtime role edit in place so the next Play must restore the authored snapshot.
                Require(_body!.SetShapeRole(19, _explicitBinding ? default : Alternate));
                CheckRole(_explicitBinding ? default : Alternate);
                _finished = true;
                Print(true, "");
            }
            if (_elapsed > 15) throw new InvalidOperationException("Role transition timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void ReadPairs(ContactStream stream, bool alternate)
    {
        foreach (ref readonly var contact in stream.Read())
        {
            if (!contact.Sensor || !contact.SelfEntity.Equals(Entity) || contact.SelfComponentId != _body!.ComponentId ||
                contact.OtherEntity.Name != "ContactHurt" || contact.OtherShapeId != 23 ||
                (contact.SelfShapeId != 19 && contact.SelfShapeId != 20))
                throw new InvalidOperationException("Role endpoint mismatch.");

            ulong identity = ((ulong)contact.SelfBodySlot << 32) | contact.SelfBodyGeneration;
            int index = Array.IndexOf(_identities, identity);
            if (index < 0)
            {
                if (_stage > 2 || _identities[_stage] != 0) throw new InvalidOperationException("Unexpected role body identity.");
                index = _stage;
                _identities[index] = identity;
            }
            if (contact.Phase != ContactPhases.End && index != _stage)
                throw new InvalidOperationException("Retired role produced stale contact.");
            if (alternate != (index == 1 && contact.SelfShapeId == 19))
                throw new InvalidOperationException("Retired endpoint role snapshot lost or new role misrouted.");

            if (alternate)
            {
                if (contact.Phase == ContactPhases.Begin) ++_alternateBegin;
                else if (contact.Phase == ContactPhases.Persist) ++_alternatePersist;
                else ++_alternateEnd;
            }
            else
            {
                if (contact.Phase == ContactPhases.Begin) ++_begins[index];
                else if (contact.Phase == ContactPhases.Persist) ++_persists[index];
                else ++_ends[index];
            }
        }
    }

    private void CheckRole(ShapeRole role)
    {
        if (_body!.GetShape(1, out var shape) != PhysicsError.None || shape.ShapeId != 19 ||
            shape.ContactRole != role || !shape.Sensor || !shape.QueryEnabled ||
            _body.GetShape(2, out var unchanged) != PhysicsError.None || unchanged.ContactRole != ShapeRoles.Attack)
            throw new InvalidOperationException("Role mutation changed flags/sibling or Play did not restore role.");
    }

    private static void Require(PhysicsError error)
    {
        if (error != PhysicsError.None) throw new InvalidOperationException($"Role mutation: {error}");
    }

    private void Print(bool success, string error) => Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
    {
        marker = "physics.contact.stream", owner = Entity.Name, success, error, roleTransition = true, explicitBinding = _explicitBinding, finalRoleChanged = _finished && success,
        attackBegins = _begins, attackPersists = _persists, attackEnds = _ends, bodies = _identities,
        alternateBegin = _alternateBegin, alternatePersist = _alternatePersist, alternateEnd = _alternateEnd,
        targetBegin = _targetBegin, targetEnd = _targetEnd,
        alternateTargetBegin = _alternateTargetBegin, alternateTargetEnd = _alternateTargetEnd
    }));
}
