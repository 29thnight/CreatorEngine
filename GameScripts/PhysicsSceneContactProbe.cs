namespace CreatorEngine.Scripts;

public sealed partial class PhysicsSceneContactProbe : Component
{
    private ContactStream? _contacts;
    private PhysicsBodyComponent? _body;
    private Entity _previousTarget;
    private int _added, _begins, _persists;
    [SerializeField] private bool _ready;
    private static PhysicsSceneContactProbe? Current;
    private static int Runs;
    private static readonly List<ContactStream> Streams = new();

    public override void OnAddedToScene() => ++_added;

    public override void OnBeginSimulation()
    {
        _ready = false;
        ++Runs;
        Current = this;
        _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing scene body.");
        _contacts = Physics.ObserveContacts(Entity, ShapeRoles.Attack, ShapeRoles.Hurt,
            ContactPhases.All, 32);
        Streams.Add(_contacts);
    }

    public override void OnRemovingFromScene()
    {
        if (_contacts == null) return;
        // Final destruction has already cancelled Scope; DDOL leaves it alive and empty.
        try
        {
            if (!_contacts.Read().IsEmpty) throw new InvalidOperationException("Old scene contacts retained at detach.");
        }
        catch (ObjectDisposedException) { return; }
        _begins = _persists = 0;
        _ready = false;
    }

    public override void PostPhysics(float dt)
    {
        if (_ready) return;
        if (_body!.ReadState(out _) != PhysicsError.None)
            throw new InvalidOperationException("Captured body wrapper did not survive the scene boundary.");

        Entity target = default;
        foreach (ref readonly var contact in _contacts!.Read())
        {
            if (!contact.Sensor || !contact.SelfEntity.Equals(Entity) || contact.SelfComponentId != _body.ComponentId ||
                contact.OtherEntity.Name != "ContactHurt")
                throw new InvalidOperationException("Scene contact endpoint mismatch.");
            target = contact.OtherEntity;
            if (_added > 1 && target == _previousTarget)
                throw new InvalidOperationException("Old scene endpoint delivered after DDOL.");
            if (contact.Phase == ContactPhases.Begin) ++_begins;
            else if (contact.Phase == ContactPhases.Persist) ++_persists;
        }
        if (_begins == 2 && _persists >= 2)
        {
            _previousTarget = target;
            _ready = true;
            Console.WriteLine("[physics.contact.scene] " + State());
        }
    }

    public override void OnEndSimulation() => Console.WriteLine("[physics.contact.scene.end] " + State());

    [EngineCallable]
    public static string State()
    {
        int disposed = 0;
        foreach (var stream in Streams)
        {
            try { stream.Read(); }
            catch (ObjectDisposedException) { ++disposed; }
        }
        // Avoid process-wide serializer metadata caching for collectible anonymous script types.
        return string.Create(System.Globalization.CultureInfo.InvariantCulture,
            $"{{\"ready\":{(Current?._ready == true ? "true" : "false")},\"added\":{Current?._added ?? 0},\"begins\":{Current?._begins ?? 0},\"persists\":{Current?._persists ?? 0},\"bodyComponent\":{Current?._body?.ComponentId ?? 0},\"runs\":{Runs},\"streams\":{Streams.Count},\"disposed\":{disposed}}}");
    }
}
