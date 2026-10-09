namespace CreatorEngine.Scripts;

public sealed partial class PhysicsContactFaultProbe : Component
{
    [SerializeField] private bool _overflow;
    private ContactStream? _contacts;
    private bool _faultOwner, _finished;
    private float _elapsed;
    private int _healthyContacts;
    private static int FaultCalls, FaultDisables, Required;
    private static bool ReadRejected;

    public override void OnBeginSimulation()
    {
        _faultOwner = Entity.Name == "ContactAttack";
        if (_faultOwner)
        {
            FaultCalls = FaultDisables = Required = 0;
            ReadRejected = false;
        }

        _contacts = Physics.ObserveContacts(Entity,
            _faultOwner ? ShapeRoles.Attack : ShapeRoles.Hurt,
            _faultOwner ? ShapeRoles.Hurt : ShapeRoles.Attack,
            ContactPhases.All, _faultOwner && _overflow ? 1 : 64);
        PhysicsContactStreamProbe.Track(_contacts);
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;
        _elapsed += dt;

        if (_faultOwner)
        {
            // Wait for actual SDK contacts so the failure exercises product delivery.
            if (_contacts!.RequiredCapacity == 0) return;
            ++FaultCalls;
            Required = _contacts.RequiredCapacity;
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
            {
                marker = "physics.contact.fault", overflow = _overflow,
                overflowed = _contacts.Overflowed, required = Required
            }));
            try { _contacts.Read(); }
            catch (InvalidOperationException error) when (_overflow &&
                error.Message == "Contact stream capacity exceeded; partial gameplay results are unavailable.")
            {
                ReadRejected = true;
                throw;
            }
            if (_overflow) throw new InvalidOperationException("Overflow Read unexpectedly returned partial contacts.");
            throw new InvalidOperationException("PhysicsContactFaultProbe injected PostPhysics exception.");
        }

        _healthyContacts += _contacts!.Read().Length;
        if (_elapsed < .3f) return;

        var failed = Entity.Find("ContactAttack").GetComponent<PhysicsContactFaultProbe>();
        if (failed == null || failed.Enabled || FaultCalls != 1 || FaultDisables != 1 ||
            _healthyContacts < 4 || (_overflow && (Required < 2 || !ReadRejected)))
            throw new InvalidOperationException("Contact fault isolation incomplete.");

        _finished = true;
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
        {
            marker = "physics.contact.stream", owner = Entity.Name, success = true, error = "",
            contactFault = true, overflow = _overflow, faultCalls = FaultCalls,
            faultDisables = FaultDisables, readRejected = ReadRejected, required = Required, healthyContacts = _healthyContacts
        }));
    }

    public override void OnDisable()
    {
        if (_faultOwner) ++FaultDisables;
    }

    public override void OnEndSimulation()
    {
        bool disposed = false;
        try { _contacts!.Read(); }
        catch (ObjectDisposedException) { disposed = true; }

        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
        {
            marker = "physics.contact.fault.cleanup", owner = Entity.Name, disposed
        }));
    }
}
