using System.Runtime.InteropServices;
using CreatorEngine;

unsafe class Program
{
    static readonly ObjectHandle Owner = new(17, 3);
    static ulong current = 101;
    static float velocity;
    static int checks;

    static void Check(bool value, string name)
    {
        ++checks;
        if (!value) throw new InvalidOperationException(name);
    }

    static void Main()
    {
        // Real managed wrappers and unmanaged ABI calls; the native registry is modeled here.
        var api = new ScriptApiTable
        {
            Version = Native.ExpectedVersion,
            StructSize = sizeof(ScriptApiTable),
            Body_Find = &Find,
            Body_Read = &Read,
            Body_Velocity = &Velocity,
            Body_Remove = &Remove
        };
        Check(Native.Bind(&api), "bind current ABI");

        var entity = new Entity(Owner);
        var old = entity.GetComponent<PhysicsBodyComponent>()!;
        Check(old.ComponentId == 101, "lookup captures original identity");
        Check(old.Remove() == PhysicsError.None, "remove original body");
        Check(entity.GetComponent<PhysicsBodyComponent>() is null, "owner has no body after removal");

        current = 202;
        velocity = 7;
        var replacement = entity.GetComponent<PhysicsBodyComponent>()!;
        Check(replacement.ComponentId == 202, "same owner lookup captures replacement identity");
        Check(old.ComponentId == 101, "old wrapper retains identity");
        Check(old.ReadState(out var stale) == PhysicsError.StaleHandle && stale.Mass == 0,
            "old read fails and resets output");
        Check(old.SetVelocity(new(99, 0, 0)) == PhysicsError.StaleHandle && velocity == 7,
            "old mutation cannot modify replacement");
        Check(old.Remove() == PhysicsError.StaleHandle && current == 202,
            "old removal cannot remove replacement");
        Check(PhysicsBodyComponent.Find(entity, 101) is null, "explicit old lookup cannot retarget");
        Check(PhysicsBodyComponent.Find(entity, 202)?.ComponentId == 202, "explicit new lookup succeeds");
        Check(replacement.SetVelocity(new(11, 0, 0)) == PhysicsError.None && velocity == 11,
            "replacement remains writable");
        Check(replacement.ReadState(out var state) == PhysicsError.None && state.LinearVelocity.X == 11,
            "replacement state is independent");
        Check(PhysicsBodyComponent.Find(new Entity(new(17, 4)), 202) is null,
            "owner generation isolates replacement");

        Native.Bind(null);
        Check(old.ReadState(out _) == PhysicsError.WrongPhase, "unbound phase takes precedence");
        Console.WriteLine($"PHYSICS_BODY_IDENTITY_OK checks={checks} version={Native.ExpectedVersion} backend=modeled");
    }

    [UnmanagedCallersOnly]
    static ulong Find(ObjectHandle owner) => owner == Owner ? current : 0;

    [UnmanagedCallersOnly]
    static int Read(ObjectHandle owner, ulong id, PhysicsBodyState* output)
    {
        *output = default;
        if (owner != Owner || id == 0 || id != current) return (int)PhysicsError.StaleHandle;
        output->Mass = 1;
        output->LinearVelocity = new(velocity, 0, 0);
        return (int)PhysicsError.None;
    }

    [UnmanagedCallersOnly]
    static int Velocity(ObjectHandle owner, ulong id, Float3 linear, Float3 angular)
    {
        if (owner != Owner || id == 0 || id != current) return (int)PhysicsError.StaleHandle;
        velocity = linear.X;
        return (int)PhysicsError.None;
    }

    [UnmanagedCallersOnly]
    static int Remove(ObjectHandle owner, ulong id)
    {
        if (owner != Owner || id == 0 || id != current) return (int)PhysicsError.StaleHandle;
        current = 0;
        return (int)PhysicsError.None;
    }
}
