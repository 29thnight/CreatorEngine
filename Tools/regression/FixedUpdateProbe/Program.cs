using CreatorEngine;
using System.Runtime.InteropServices;

unsafe class Program
{
    private static int _checks;
    private static int _next;
    private static int _faults;
    private static readonly HashSet<ObjectHandle> Alive = new();
    static void Check(bool value, string reason) { ++_checks; if (!value) throw new Exception(reason); }
    [UnmanagedCallersOnly] static int IsAlive(ObjectHandle handle) => Alive.Contains(handle) ? 1 : 0;
    [UnmanagedCallersOnly] static int Asset(AssetLinkABI* l, uint k, TextureAssetVariantABI* v, AssetToken* t) => -1;
    [UnmanagedCallersOnly] static void Cut(ObjectHandle h) { }
    [UnmanagedCallersOnly] static int TickStatus(SimulationStatus* s) { *s = default; return 0; }
    [UnmanagedCallersOnly] static int Submit(ulong e, ulong t, uint k, byte* p, int n, ulong* s) { *s = 0; return 1; }
    [UnmanagedCallersOnly] static int Read(ulong e, ulong t, SimulationSnapshot* s, byte* p, int n) { *s = default; return 7; }
    [UnmanagedCallersOnly] static void Log(int level, byte* message, byte* file, int line, byte* member) { if (level == 3) ++_faults; }
    static (TestComponent component, int id) Make(Action<TestComponent, float>? action = null, bool start = true, bool failInit = false, bool failBegin = false)
    {
        string type = "FixedUpdateTest" + ++_next;
        var component = new TestComponent { Action = action, FailInit = failInit, FailBegin = failBegin };
        var owner = new ObjectHandle((uint)_next, 1);
        Alive.Add(owner);
        ScriptFactory.Register(type, () => component);
        ScriptFactory.RegisterLifecycle(type, new ScriptLifecycle
        {
            OnInitialized = script => ((TestComponent)script).OnInitialized(),
            OnBeginSimulation = script => ((TestComponent)script).OnBeginSimulation(),
            OnEndSimulation = script => ((TestComponent)script).OnEndSimulation(),
            OnUninitializing = script => ((TestComponent)script).OnUninitializing(),
            OnTickSimulate = (script, dt) => ((TestComponent)script).OnTickSimulate(dt)
        });
        int id = ScriptFactory.Create(owner, type);
        ScriptRegistry.DispatchLifecycle(id, 0);
        ScriptRegistry.DispatchLifecycle(id, 1);
        if (start) ScriptRegistry.DispatchLifecycle(id, 2);
        return (component, id);
    }
    static void Main()
    {
        var api = new ScriptApiTable { Version = Native.ExpectedVersion, StructSize = sizeof(ScriptApiTable),
            Entity_IsAlive = &IsAlive, Asset_RequestTyped = &Asset, Asset_TryAcquireTyped = &Asset,
            Camera_NotifyCameraCut = &Cut, PrintLog = &Log,
            Simulation_Status = &TickStatus, Simulation_Submit = &Submit, Simulation_Read = &Read };
        --api.Version;
        Check(!Native.Bind(&api), "previous host contract version rejected");
        ++api.Version;
        api.Simulation_Read = null;
        Check(!Native.Bind(&api), "missing snapshot copy entry rejected");
        api.Simulation_Read = &Read;
        Check(Native.Bind(&api), "real Native table binds");
        Check(Simulation.GetStatus(out _) == SimulationResult.Ok, "simulation status crosses appended ABI");
        Check(Task.Run(() => Simulation.GetStatus(out _)).GetAwaiter().GetResult() == SimulationResult.WrongThread, "worker simulation API rejected");
        ScriptFactory.Initialize();
        Check(typeof(Component).GetMethod("OnSimulate") == null && typeof(Component).GetProperty("Scope") == null,
            "removed authoring API");
        Check(typeof(Component).Assembly.GetType("CreatorEngine.SimulationScope") == null, "no public scope type");
        var (normal, normalId) = Make();
        Check(normal.Calls == 0, "lifecycle does not invoke update");
        delegate* unmanaged<float, int> update = &Bootstrap.OnTickSimulate;
        Check(update(1f / 60) == ScriptRegistry.ActiveCount, "real unmanaged bootstrap entry");

        Check(normal.Calls == 1 && normal.Dt == 1f / 60 && normal.Thread == Environment.CurrentManagedThreadId, "fixed dt on owner thread");
        var (initFailed, _) = Make(failInit: true);
        var (beginFailed, _) = Make(failBegin: true);
        ScriptRegistry.UpdateTick(1f / 60);
        Check(initFailed.Calls == 0 && beginFailed.Calls == 0, "failed init/begin never update");
        int faultBaseline = _faults;
        int normalBaseline = normal.Calls;
        var (notStarted, _) = Make(start: false);
        normal.Enabled = false;
        ScriptRegistry.UpdateTick(1f / 60);
        Check(normal.Calls == normalBaseline && notStarted.Calls == 0, "disabled and unstarted do not update");
        normal.Enabled = true;
        TestComponent? spawned = null;
        var (spawner, _) = Make((self, dt) => { if (spawned == null) spawned = Make().component; ScriptRegistry.FlushRegistrations(); });
        ScriptRegistry.UpdateTick(1f / 60);
        Check(spawned != null && spawned.Calls == 0, "reentrant registration joins next batch");
        ScriptRegistry.UpdateTick(1f / 60);
        Check(spawned!.Calls == 1, "spawned script updates on next tick");
        var (bad, _) = Make((self, dt) => throw new InvalidOperationException("injected"));
        int before = normal.Calls;
        ScriptRegistry.UpdateTick(1f / 60);
        ScriptRegistry.UpdateTick(1f / 60);
        Check(bad.Calls == 1 && !bad.Enabled && normal.Calls == before + 2 && _faults == faultBaseline + 1, "exception isolates one script");
        var (destroyed, destroyedId) = Make((self, dt) => ScriptFactory.Destroy(self.InstanceId));
        ScriptRegistry.UpdateTick(1f / 60);
        ScriptRegistry.UpdateTick(1f / 60);
        Check(destroyed.Calls == 1 && destroyed.Ends == 1, "self removal does not skip teardown or repeat update");
        var (ddol, ddolId) = Make();
        ScriptRegistry.DispatchLifecycle(ddolId, 4);
        ScriptRegistry.DispatchLifecycle(ddolId, 1);
        ScriptRegistry.UpdateTick(1f / 60);
        Check(ddol.Begins == 1 && ddol.Calls == 1 && ddol.Ends == 0, "DDOL retains live update and lifetime");
        int cleaned = 0;
        ddol.Lifetime.RegisterCleanup(() => { ++cleaned; throw new Exception("cleanup injection"); });
        ddol.Lifetime.RegisterCleanup(() => ++cleaned);
        ScriptRegistry.DispatchLifecycle(ddolId, 3);
        Check(cleaned == 2 && ddol.Ends == 1, "cleanup fault does not skip siblings or end hook");
        ScriptRegistry.UpdateTick(1f / 60);
        Check(ddol.Calls == 1, "ended component never resumes");
        var (orphan, _) = Make();
        Alive.Remove(orphan.Entity.Handle);
        ScriptRegistry.SweepOrphans();
        ScriptRegistry.FlushRegistrations();
        Check(orphan.Ends == 1 && orphan.Uninitialized == 1, "orphan cleanup survives public scope removal");
        var rejected = new TestComponent();
        ScriptFactory.Register("RejectedDuringReload", () => rejected);
        var (cleanupReentry, _) = Make();
        cleanupReentry.End = () =>
        {
            try { ScriptFactory.Create(new ObjectHandle(900, 1), "RejectedDuringReload"); }
            catch (InvalidOperationException) { }
        };
        var (reloader, _) = Make((self, dt) => ScriptRegistry.Clear());
        var (afterReload, _) = Make();
        ScriptRegistry.UpdateTick(1f / 60);
        Check(afterReload.Calls == 0 && afterReload.Ends == 1 && ScriptRegistry.ActiveCount == 0, "reload invalidates current batch");
        Check(reloader.Uninitialized == 1, "reload cleanup once");
        Check(rejected.InstanceId > 0 && ScriptFactory.Find(rejected.InstanceId) == null && !rejected.IsAlive,
            "rejected reentrant registration does not retain an unbound instance");
        Native.Unbind();
        VerifyResidentHostContext();
        Console.WriteLine($"FIXED_UPDATE_MANAGED_OK checks={_checks}");
    }

    private static void VerifyResidentHostContext()
    {
        // hostfxr's component context is distinct from Default. This reproduces
        // the product hosting boundary that a normal dotnet test process hides.
        var context = new System.Runtime.Loader.AssemblyLoadContext("EngineHostContractProbe", isCollectible: false);
        var bindings = context.LoadFromAssemblyPath(typeof(Component).Assembly.Location);
        var factory = bindings.GetType("CreatorEngine.ScriptFactory", throwOnError: true)!;
        factory.GetMethod("Initialize")!.Invoke(null, null);
        Check((int)factory.GetProperty("RegisteredCount")!.GetValue(null)! == 0, "isolated engine host initializes its own F# runtime");
        Check(context.Assemblies.Any(assembly => assembly.GetName().Name == "CreatorEngine.ManagedRuntime"), "F# runtime stays with its engine binding context");
        Check(context.Assemblies.Any(assembly => assembly.GetName().Name == "FSharp.Core"), "engine runtime FSharp.Core stays in the resident host context");
    }
}

sealed class TestComponent : Component
{
    internal Action<TestComponent, float>? Action;
    internal int Calls, Begins, Ends, Uninitialized, Thread;
    internal float Dt;
    internal Action? End;
    internal bool FailInit, FailBegin;
    public void OnInitialized() { if (FailInit) throw new InvalidOperationException("init injection"); }
    public void OnBeginSimulation() { ++Begins; if (FailBegin) throw new InvalidOperationException("begin injection"); }
    public void OnEndSimulation() { ++Ends; End?.Invoke(); }
    public void OnUninitializing() { ++Uninitialized; }
    public void OnTickSimulate(float dt) { ++Calls; Dt = dt; Thread = Environment.CurrentManagedThreadId; Action?.Invoke(this, dt); }
}
