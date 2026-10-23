using CreatorEngine;
using System.Runtime.InteropServices;

// Controlled native ABI only. The component lifetime, router, buffer and tick
// execution below are the real product implementations, including the F# runtime.
internal sealed class ProbeComponent : Component
{
    internal ProbeComponent()
    {
        MarkInitialized();
        MarkInitializeSucceeded();
        MarkBeginSucceeded();
    }
}

internal static unsafe class ContactProbeHost
{
    private static int next;
    [UnmanagedCallersOnly] private static int IsAlive(ObjectHandle owner) => 1;
    [UnmanagedCallersOnly] private static int Asset(AssetLinkABI* link, uint kind, TextureAssetVariantABI* variant, AssetToken* token) => -1;
    [UnmanagedCallersOnly] private static void Cut(ObjectHandle owner) { }
    [UnmanagedCallersOnly] private static int Status(SimulationStatus* status) { *status = default; return 0; }
    [UnmanagedCallersOnly] private static int Submit(ulong epoch, ulong tick, uint kind, byte* payload, int count, ulong* sequence) => 1;
    [UnmanagedCallersOnly] private static int Read(ulong epoch, ulong tick, SimulationSnapshot* snapshot, byte* payload, int count) => 7;

    internal static void Bind()
    {
        var api = new ScriptApiTable
        {
            Version = Native.ExpectedVersion,
            StructSize = sizeof(ScriptApiTable),
            Entity_IsAlive = &IsAlive,
            Asset_RequestTyped = &Asset,
            Asset_TryAcquireTyped = &Asset,
            Camera_NotifyCameraCut = &Cut,
            Simulation_Status = &Status,
            Simulation_Submit = &Submit,
            Simulation_Read = &Read
        };
        if (!Native.Bind(&api)) throw new Exception("Native contact probe contract did not bind.");
        ScriptFactory.Initialize();
    }

    internal static void FixedTick(Action read)
    {
        string name = "ContactTickDriver" + ++next;
        ScriptFactory.Register(name, () => new ProbeComponent());
        ScriptFactory.RegisterLifecycle(name, new ScriptLifecycle { OnTickSimulate = (_, _) => read() });
        int id = ScriptFactory.Create(new ObjectHandle((uint)next, 1), name);
        ScriptRegistry.UpdateTick(1f / 60);
        ScriptFactory.Destroy(id);
        ScriptRegistry.FlushRegistrations();
    }
}
