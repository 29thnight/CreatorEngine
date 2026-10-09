namespace CreatorEngine.Scripts;

// The observer has no body. Only its script is disabled; the controller and SDK pairs remain active.
public sealed partial class PhysicsSubscriberContactProbe : Component
{
    private ContactStream? _pairs, _targets, _beginOnly;
    private PhysicsSubscriberContactProbe? _observer;
    private PhysicsBodyComponent? _body;
    private bool _controller, _finished;
    private int _stage, _begins, _persists, _ends, _targetBegins, _targetPersists, _targetEnds, _beginOnlyCount;
    private int _disables, _enables, _callbacks, _disabledCallbacks, _enableTargetPersists;
    private float _elapsed, _stageAt;
    private ulong _lastTick, _enableAfterTick, _seedTick;

    public override void OnBeginSimulation()
    {
        _controller = Entity.Name == "ContactAttack";
        var owner = Entity.Find("ContactAttack");
        if (!owner.IsAlive) throw new InvalidOperationException("Missing attack owner.");

        _pairs = Physics.ObserveContacts(owner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 32);
        PhysicsContactStreamProbe.Track(_pairs);
        if (_controller)
        {
            _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing controller body.");
            _observer = Entity.Find("ContactObserver").GetComponent<PhysicsSubscriberContactProbe>()
                ?? throw new InvalidOperationException("Missing bodyless observer.");
        }
        else
        {
            if (Entity.HasComponent<PhysicsBodyComponent>()) throw new InvalidOperationException("Observer must have no body.");
            _targets = Physics.ObserveContacts(owner, ShapeRoles.Attack, ShapeRoles.Hurt,
                ContactPhases.All, 32, ContactGrouping.SensorTargets);
            _beginOnly = Physics.ObserveContacts(owner, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.Begin, 32);
            PhysicsContactStreamProbe.Track(_targets);
            PhysicsContactStreamProbe.Track(_beginOnly);
        }
    }

    public override void OnDisable()
    {
        if (_pairs == null || _controller || _finished) return;

        RequireEmpty();
        ++_disables;
        _disabledCallbacks = _callbacks;
    }

    public override void OnEnable()
    {
        if (_pairs == null || _controller || _finished) return;

        RequireEmpty();
        if (_callbacks != _disabledCallbacks) throw new InvalidOperationException("Disabled observer callback ran.");
        ++_enables;
    }

    private void RequireEmpty()
    {
        if (!_pairs!.Read().IsEmpty || !_targets!.Read().IsEmpty || !_beginOnly!.Read().IsEmpty ||
            _pairs.Overflowed || _targets.Overflowed || _beginOnly.Overflowed)
            throw new InvalidOperationException("Suspended observer retained contact history.");
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;

        try
        {
            ++_callbacks;
            _elapsed += dt;
            foreach (ref readonly var contact in _pairs!.Read())
            {
                if (!contact.Sensor || (contact.SelfShapeId != 19 && contact.SelfShapeId != 20) ||
                    contact.OtherShapeId != 23 || contact.OtherEntity.Name != "ContactHurt" || contact.Tick < _lastTick)
                    throw new InvalidOperationException("Subscriber contact endpoint/tick mismatch.");

                _lastTick = contact.Tick;
                if (!_controller && _enables > 0 && contact.Tick <= _enableAfterTick)
                    throw new InvalidOperationException("Disabled contact tick replayed.");

                if (contact.Phase == ContactPhases.Begin)
                {
                    ++_begins;
                    if (!_controller && _enables > 0 && _seedTick == 0) _seedTick = contact.Tick;
                }
                else if (contact.Phase == ContactPhases.Persist) ++_persists;
                else ++_ends;
            }
            if (!_controller)
            {
                foreach (ref readonly var contact in _targets!.Read())
                {
                    if (contact.Phase == ContactPhases.Begin) ++_targetBegins;
                    else if (contact.Phase == ContactPhases.Persist) ++_targetPersists;
                    else ++_targetEnds;
                }
                foreach (ref readonly var contact in _beginOnly!.Read())
                {
                    if (contact.Phase != ContactPhases.Begin) throw new InvalidOperationException("Begin-only phase mismatch.");
                    ++_beginOnlyCount;
                }
                if (_begins > 4 || _targetBegins > 2 || _beginOnlyCount > 4 || _ends > 2 || _targetEnds > 1)
                    throw new InvalidOperationException("Observer replayed history or overlap Begin.");
                return;
            }

            var observer = _observer!;
            if (_stage == 0 && observer._begins == 2 && observer._persists >= 2)
            {
                observer.Enabled = false;
                if (observer.Enabled || observer._disables != 1) throw new InvalidOperationException("Native Disable dispatch missing.");
                observer.RequireEmpty();
                Move(10);
                _stageAt = _elapsed;
                _stage = 1;
            }
            else if (_stage == 1 && _ends == 2 && _elapsed - _stageAt > .15f)
            {
                observer.RequireEmpty();
                if (observer._callbacks != observer._disabledCallbacks) throw new InvalidOperationException("Disabled PostPhysics ran.");
                Move(0);
                _stage = 2;
                _stageAt = _elapsed;
            }
            else if (_stage == 2 && _begins == 4 && _elapsed - _stageAt > .15f)
            {
                observer.RequireEmpty();
                observer._enableAfterTick = _lastTick;
                observer._enableTargetPersists = observer._targetPersists;
                observer.Enabled = true;
                if (!observer.Enabled || observer._enables != 1) throw new InvalidOperationException("Native Enable dispatch missing.");
                observer.RequireEmpty();
                _stage = 3;
            }
            else if (_stage == 3 && observer._begins == 4 && observer._targetBegins == 2 &&
                observer._beginOnlyCount == 4 && observer._targetPersists > observer._enableTargetPersists)
            {
                if (observer._ends != 0 || observer._targetEnds != 0 || observer._seedTick <= observer._enableAfterTick)
                    throw new InvalidOperationException("Missed disabled End was replayed.");
                Move(10);
                _stage = 4;
            }
            else if (_stage == 4 && _ends == 4 && observer._ends == 2 && observer._targetEnds == 1)
            {
                _stage = 5;
                _stageAt = _elapsed;
            }
            else if (_stage == 5 && _elapsed - _stageAt > .1f)
            {
                if (observer._begins != 4 || observer._targetBegins != 2 || observer._beginOnlyCount != 4 ||
                    observer._ends != 2 || observer._targetEnds != 1 || _begins != 4 || _ends != 4)
                    throw new InvalidOperationException("Reactivated events replayed after final exit.");
                observer._finished = _finished = true;
                Print(true, "");
            }
            if (_elapsed > 15) throw new InvalidOperationException("Subscriber lifetime product timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void Move(float x)
    {
        Transform.WorldPosition = new Float3(x, 0, 0);
        if (_body!.SetVelocity(default) != PhysicsError.None) throw new InvalidOperationException("Controller move failed.");
    }

    private void Print(bool success, string error)
    {
        var observer = _observer ?? this;
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { marker = "physics.contact.stream",
            owner = Entity.Name, success, error, subscriberLifetime = true,
            sensorBegin = _begins, sensorPersist = _persists, sensorEnd = _ends,
            observerBegin = observer._begins, observerEnd = observer._ends,
            targetBegin = observer._targetBegins, targetEnd = observer._targetEnds,
            beginOnly = observer._beginOnlyCount, disables = observer._disables, enables = observer._enables,
            callbacksBeforeDisable = observer._disabledCallbacks,
            reactivatedPersists = observer._targetPersists - observer._enableTargetPersists, enableAfterTick = observer._enableAfterTick, seedTick = observer._seedTick }));
    }
}
