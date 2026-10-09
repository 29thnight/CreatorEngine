namespace CreatorEngine.Scripts;

public sealed partial class PhysicsSolidContactProbe : Component
{
    private ContactStream? _pairs, _targets;
    private PhysicsBodyComponent? _body;
    private readonly int[] _begins = new int[2], _persists = new int[2], _ends = new int[2];
    private int _stage, _partialPersists;
    private float _elapsed, _settled;
    private bool _finished;

    public override void OnBeginSimulation()
    {
        _body = GetComponent<PhysicsBodyComponent>() ?? throw new InvalidOperationException("Missing solid body.");
        _pairs = Physics.ObserveContacts(Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 128);
        _targets = Physics.ObserveContacts(Entity, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 128, ContactGrouping.SensorTargets);
        PhysicsContactStreamProbe.Track(_pairs);
        PhysicsContactStreamProbe.Track(_targets);
    }

    public override void PostPhysics(float dt)
    {
        if (_finished) return;

        try
        {
            _elapsed += dt;
            var pairs = _pairs!.Read();
            var targets = _targets!.Read();
            if (pairs.Length != targets.Length) throw new InvalidOperationException("Solid target grouping lost pairs.");

            for (int i = 0; i < pairs.Length; ++i)
            {
                ref readonly var contact = ref pairs[i];
                ref readonly var grouped = ref targets[i];
                int shape = contact.SelfShapeId == 19 ? 0 : contact.SelfShapeId == 20 ? 1 : -1;
                if (shape < 0 || contact.Sensor || !contact.SelfEntity.Equals(Entity) || contact.OtherEntity.Name != "ContactHurt" ||
                    contact.SelfComponentId != _body!.ComponentId || contact.OtherShapeId != 23 || contact.Tick == 0 ||
                    grouped.SelfShapeId != contact.SelfShapeId || grouped.Phase != contact.Phase || grouped.Tick != contact.Tick ||
                    grouped.ContactCount != contact.ContactCount || grouped.RequiredContacts != contact.RequiredContacts)
                    throw new InvalidOperationException("Solid endpoint or grouped payload mismatch.");

                if (contact.Phase == ContactPhases.Begin) ++_begins[shape];
                else if (contact.Phase == ContactPhases.End) ++_ends[shape];
                else
                {
                    ++_persists[shape];
                    if (_stage == 1 && shape == 0 && _ends[1] == 1) ++_partialPersists;
                }
            }

            if (_stage == 0 && _begins[0] == 1 && _begins[1] == 1 && _persists[0] >= 2 && _persists[1] >= 2)
            {
                Transform.WorldPosition = new Float3(0, 0, 1.5f);
                _stage = 1;
            }
            else if (_stage == 1 && _ends[1] == 1 && _ends[0] == 0 && _partialPersists >= 2)
            {
                Transform.WorldPosition = new Float3(0, 0, 5);
                _stage = 2;
            }
            else if (_stage == 2 && _ends[0] == 1)
            {
                _stage = 3;
                _settled = _elapsed;
            }
            else if (_stage == 3 && _elapsed - _settled > .1f)
            {
                if (_begins[0] != 1 || _begins[1] != 1 || _ends[0] != 1 || _ends[1] != 1 || !pairs.IsEmpty)
                    throw new InvalidOperationException("Solid counts or stale replay mismatch.");

                _finished = true;
                Print(true, "");
            }

            if (_elapsed > 15) throw new InvalidOperationException("Solid contact timeout.");
        }
        catch (Exception error)
        {
            _finished = true;
            Print(false, error.Message);
            throw;
        }
    }

    private void Print(bool success, string error) => Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
    {
        marker = "physics.contact.stream", owner = Entity.Name, success, error, solidContacts = true,
        begins = _begins, persists = _persists, ends = _ends, partialPersists = _partialPersists, stage = _stage
    }));
}
