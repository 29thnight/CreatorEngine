namespace CreatorEngine.Scripts;

public sealed partial class PhysicsContactStreamProbe : Component
{
    [SerializeField] private bool _retireTarget;
    [SerializeField] private bool _removeBodyOnly;
    private PhysicsBodyComponent? _removedBody;
    private int _staleChecks;
    private Entity _retiredTarget;
    private ulong _retiredComponent;
    [SerializeField] private bool _groupTargets;
    private ContactStream? _grouped;
    private int _groupBegin, _groupPersist, _groupEnd;
    private ulong _groupTick;
    private bool _partialExit;
    [SerializeField] private string _lateObserverPrefab = "";
    internal static Entity AttackOwner;
    internal static ulong ObserverSpawnTick;
    internal static bool LateReady;
    internal static void Track(ContactStream stream) => RetiredStreams.Add(stream);
    private ContactStream? _contacts;
    private PhysicsBodyComponent? _body;
    private bool _attack, _finished;
    private int _stage, _sensorBegin, _sensorPersist, _sensorEnd, _begin, _persist, _end;
    private float _elapsed, _stageAt;
    private ulong _lastTick;
    private static readonly List<ContactStream> RetiredStreams = new();
    private static int Started, Frames;

    public override void OnBeginSimulation()
    {
        ++Started;
        if (_removeBodyOnly)
        {
            var restored = Entity.Find("ContactHurt").GetComponent<PhysicsBodyComponent>();
            if (restored == null || restored.GetShape(0, out var shape) != PhysicsError.None ||
                shape.ShapeId != 23 || shape.ContactRole != ShapeRoles.Hurt)
                throw new InvalidOperationException("Removed body was not restored before Play.");
        }
        _attack = Entity.Name == "ContactAttack";
        if (_attack)
        {
            AttackOwner = Entity;
            ObserverSpawnTick = 0;
            LateReady = false;
        }
        _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing contact body.");
        if (_body.GetShape(0, out var authored) != PhysicsError.None ||
            authored.ContactRole != (_attack ? ShapeRoles.Attack : ShapeRoles.Hurt))
            throw new InvalidOperationException("Stored contact role missing.");

        bool bindingRejected = false;
        try { Physics.BindContactRole(_body, _attack ? 17u : 23u, ShapeRoles.Attack); }
        catch (InvalidOperationException) { bindingRejected = true; }
        if (!bindingRejected) throw new InvalidOperationException("Authored role accepted script rebinding.");

        _contacts = Physics.ObserveContacts(Entity, _attack ? ShapeRoles.Attack : ShapeRoles.Hurt,
            _attack ? ShapeRoles.Hurt : ShapeRoles.Attack, ContactPhases.All, capacity: 64);
        RetiredStreams.Add(_contacts);
        if (_groupTargets)
        {
            _grouped = Physics.ObserveContacts(Entity, _attack ? ShapeRoles.Attack : ShapeRoles.Hurt,
                _attack ? ShapeRoles.Hurt : ShapeRoles.Attack, ContactPhases.All, 64, ContactGrouping.SensorTargets);
            RetiredStreams.Add(_grouped);
        }
    }

    public override void PostPhysics(float dt)
    {
        ++Frames;
        if (_finished) return;
        if (_retireTarget) { ReadRetirement(dt); return; }

        try
        {
            _elapsed += dt;

            foreach (ref readonly var contact in _contacts!.Read())
            {
                if (!contact.SelfEntity.Equals(Entity) || contact.OtherEntity.Name != (_attack ? "ContactHurt" : "ContactAttack") ||
                    contact.SelfComponentId != _body!.ComponentId || contact.OtherComponentId == 0 ||
                    contact.Tick == 0 || contact.Tick < _lastTick ||
                    (_attack ? contact.OtherShapeId != 23 : contact.SelfShapeId != 23))
                    throw new InvalidOperationException("Contact endpoint/orientation/tick mismatch.");

                var attackShape = _attack ? contact.SelfShapeId : contact.OtherShapeId;
                if (attackShape != (contact.Sensor ? 19u : 17u) && !(_groupTargets && contact.Sensor && attackShape == 20))
                    throw new InvalidOperationException("Contact shape mismatch.");

                _lastTick = contact.Tick;

                if (contact.Sensor)
                {
                    if (contact.Phase == ContactPhases.Begin) ++_sensorBegin;
                    else if (contact.Phase == ContactPhases.Persist) ++_sensorPersist;
                    else if (contact.Phase == ContactPhases.End) ++_sensorEnd;
                }
                else
                {
                    if (contact.Phase == ContactPhases.Begin) ++_begin;
                    else if (contact.Phase == ContactPhases.Persist) ++_persist;
                    else if (contact.Phase == ContactPhases.End) ++_end;
                }
            }

            if (_grouped != null)
            {
                foreach (ref readonly var contact in _grouped.Read())
                {
                    if (!contact.Sensor) continue;
                    if (!contact.SelfEntity.Equals(Entity) || contact.OtherEntity.Name != (_attack ? "ContactHurt" : "ContactAttack"))
                        throw new InvalidOperationException("Grouped endpoint mismatch.");
                    if (contact.Phase == ContactPhases.Begin) ++_groupBegin;
                    else if (contact.Phase == ContactPhases.End) ++_groupEnd;
                    else
                    {
                        if (contact.Tick <= _groupTick) throw new InvalidOperationException("Grouped Persist repeated per tick.");
                        ++_groupPersist;
                    }
                    _groupTick = contact.Tick;
                }
                if (_groupBegin > 1 || _groupEnd > 1 || (_sensorEnd == 1 && _groupEnd != 0))
                    throw new InvalidOperationException("Grouped target transition repeated or ended on partial exit.");
            }

            if (_attack && _stage == 0 && _sensorBegin > 0 && _sensorPersist > 0)
            {
                if (!string.IsNullOrEmpty(_lateObserverPrefab))
                {
                    if (ObserverSpawnTick == 0)
                    {
                        ObserverSpawnTick = _lastTick;
                        if (!Prefab.Load(_lateObserverPrefab).Instantiate("LateContactObserver").IsAlive)
                            throw new InvalidOperationException("Late observer prefab spawn failed.");
                    }
                    if (!LateReady) return;
                }

                if (_groupTargets && !_partialExit)
                {
                    if (_sensorBegin != 2) return;
                    Transform.WorldPosition = new Float3(.6f, 0, 0);
                    Require(_body!.SetVelocity(default), "partial sensor exit");
                    _partialExit = true;
                    _stageAt = _elapsed;
                    return;
                }
                if (_groupTargets && (_sensorEnd != 1 || _elapsed - _stageAt < .1f)) return;

                Transform.WorldPosition = new Float3(2.5f, 0, 0);
                Require(_body!.SetVelocity(default), "enter solid pair");
                _stage = 1;
                _stageAt = _elapsed;
            }
            else if (_attack && _stage == 1 && _persist > 0 && _elapsed - _stageAt > .1f)
            {
                Transform.WorldPosition = new Float3(10, 0, 0);
                Require(_body!.SetVelocity(default), "leave solid pair");
                _stage = 2;
            }

            if (_sensorBegin > 0 && _sensorPersist > 0 && _sensorEnd > 0 && _begin > 0 && _persist > 0 && _end > 0)
            {
                if (_groupTargets && (_sensorBegin != 2 || _sensorEnd != 2 || _groupBegin != 1 || _groupPersist < 2 || _groupEnd != 1))
                    throw new InvalidOperationException("Grouped target acceptance incomplete.");
                _finished = true;
                Print(true, "");
            }
            else if (_elapsed > 15)
                throw new InvalidOperationException("Contact stream product timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void ReadRetirement(float dt)
    {
        try
        {
            _elapsed += dt;
            foreach (ref readonly var contact in _contacts!.Read())
            {
                if (!contact.Sensor || !contact.SelfEntity.Equals(Entity) || contact.OtherShapeId != 23 ||
                    (contact.SelfShapeId != 19 && contact.SelfShapeId != 20) || contact.Tick == 0 || contact.Tick < _lastTick)
                    throw new InvalidOperationException("Retired endpoint mismatch.");

                _lastTick = contact.Tick;
                if (_stage == 0)
                {
                    _retiredTarget = contact.OtherEntity;
                    _retiredComponent = contact.OtherComponentId;
                    if (contact.Phase == ContactPhases.Begin) ++_sensorBegin;
                    else if (contact.Phase == ContactPhases.Persist) ++_sensorPersist;
                    else throw new InvalidOperationException("Early retirement End.");
                }
                else
                {
                    if (contact.Phase != ContactPhases.End || !contact.OtherEntity.Equals(_retiredTarget) ||
                        contact.OtherComponentId != _retiredComponent || contact.OtherEntity.IsAlive != _removeBodyOnly)
                        throw new InvalidOperationException("Deleted identity lost or stale Persist delivered.");
                    ++_sensorEnd;
                }
            }
            foreach (ref readonly var contact in _grouped!.Read())
            {
                if (!contact.Sensor || !contact.OtherEntity.Equals(_retiredTarget))
                    throw new InvalidOperationException("Retired grouped endpoint mismatch.");
                if (contact.Phase == ContactPhases.Begin) ++_groupBegin;
                else if (contact.Phase == ContactPhases.Persist)
                {
                    if (_stage != 0 || contact.Tick <= _groupTick)
                        throw new InvalidOperationException("Retired grouped stale/duplicate Persist.");
                    ++_groupPersist;
                }
                else ++_groupEnd;
                _groupTick = contact.Tick;
            }
            if (_stage == 0 && _sensorBegin == 2 && _sensorPersist > 0 && _groupBegin == 1 && _groupPersist > 0)
            {
                if (_removeBodyOnly)
                {
                    _removedBody = PhysicsBodyComponent.Find(_retiredTarget, _retiredComponent)
                        ?? throw new InvalidOperationException("Removal target wrapper missing.");
                    Require(_removedBody.Remove(), "remove exact body component");
                    ExpectStale(_removedBody.ReadState(out _));
                    ExpectStale(_removedBody.SetVelocity(default));
                    ExpectStale(_removedBody.Remove());
                }
                else _retiredTarget.Destroy();
                _stage = 1;
            }
            else if (_stage == 1 && _sensorEnd == 2 && _groupEnd == 1)
            {
                _stage = 2;
                _stageAt = _elapsed;
            }
            else if (_stage == 2 && _elapsed - _stageAt > .1f)
            {
                if (_sensorEnd != 2 || _groupEnd != 1 || _retiredTarget.IsAlive != _removeBodyOnly)
                    throw new InvalidOperationException("Retired End replay or owner lifetime mismatch.");
                if (_removeBodyOnly)
                {
                    if (_retiredTarget.HasComponent<PhysicsBodyComponent>() || PhysicsBodyComponent.Find(_retiredTarget, _retiredComponent) != null)
                        throw new InvalidOperationException("Removed body still discoverable.");
                    ExpectStale(_removedBody!.ReadState(out _));
                    ExpectStale(_removedBody.SetShapeFlags(23, true, true));
                    ExpectStale(_removedBody.ApplyForce(default));
                }
                _finished = true;
                Print(true, "");
            }
            if (_elapsed > 15) throw new InvalidOperationException("Retirement product timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void ExpectStale(PhysicsError error)
    {
        if (error != PhysicsError.StaleHandle) throw new InvalidOperationException($"Removed wrapper retargeted: {error}");
        ++_staleChecks;
    }

    private static void Require(PhysicsError error, string operation)
    {
        if (error != PhysicsError.None) throw new InvalidOperationException($"{operation}: {error}");
    }

    private void Print(bool success, string error) => Console.WriteLine(
        System.Text.Json.JsonSerializer.Serialize(new { marker = "physics.contact.stream", owner = Entity.Name,
            success, error, sensorBegin = _sensorBegin, sensorPersist = _sensorPersist, sensorEnd = _sensorEnd,
            begin = _begin, persist = _persist, end = _end, tick = _lastTick,
            componentRemoved = _removeBodyOnly, staleChecks = _staleChecks, retired = _retireTarget, deletedAlive = _retiredTarget.IsAlive, grouped = _groupTargets, groupBegin = _groupBegin, groupPersist = _groupPersist, groupEnd = _groupEnd }));

    [EngineCallable]
    public static string State() => System.Text.Json.JsonSerializer.Serialize(new { started = Started, frames = Frames });

    [EngineCallable]
    public static string Retired()
    {
        int disposed = 0;
        foreach (var stream in RetiredStreams)
        {
            try { stream.Read(); }
            catch (ObjectDisposedException) { ++disposed; }
        }

        return System.Text.Json.JsonSerializer.Serialize(new { streams = RetiredStreams.Count, disposed });
    }
}
