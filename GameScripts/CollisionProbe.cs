namespace CreatorEngine.Scripts;

/// <summary>ContactStream consumer; role bindings are authored explicitly by the simulation scripts.</summary>
public sealed partial class CollisionProbe : Component
{
    [SerializeField] private bool _logStay;
    private ContactStream? _contacts;

    public override void OnBeginSimulation()
    {
        _contacts = Physics.ObserveContacts(Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All);
    }

    public override void PostPhysics(float dt)
    {
        if (_contacts is null) return;
        foreach (ref readonly var contact in _contacts.Read())
        {
            if (contact.Phase == ContactPhases.Persist && !_logStay) continue;
            Log($"[CollisionProbe] {contact.Phase} sensor={contact.Sensor} tick={contact.Tick} shape={contact.SelfShapeId}/{contact.OtherShapeId}");
        }
    }
}
