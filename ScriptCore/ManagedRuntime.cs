using System.Reflection;
using System.Diagnostics.CodeAnalysis;

namespace CreatorEngine;

/// <summary>Typed callbacks generated from ordinary F# instance members.</summary>
public sealed class ScriptLifecycle
{
    internal static readonly ScriptLifecycle Empty = new();
    public Action<Component>? OnInitialized { get; init; }
    public Action<Component>? OnAddedToScene { get; init; }
    public Action<Component>? OnBeginSimulation { get; init; }
    public Action<Component>? OnEndSimulation { get; init; }
    public Action<Component>? OnRemovingFromScene { get; init; }
    public Action<Component>? OnUninitializing { get; init; }
    public Action<Component>? OnEnable { get; init; }
    public Action<Component>? OnDisable { get; init; }

    public Action<Component, float>? PrePhysics { get; init; }
    public Action<Component, float>? PostPhysics { get; init; }
    public Action<Component, float>? OnTickSimulate { get; init; }
}

internal interface IComponentLifetime
{
    void RegisterCleanup(Action action);
    void End();
}

// The C# assembly contains the native ABI and public binding types. All instance
// membership, lifecycle execution, cleanup and collectible loading live in F#.
internal interface IManagedRuntime
{
    int ScriptApi { get; }
    IContactRuntime Contacts { get; }
    int ActiveCount { get; }
    bool InFixedUpdate { get; }
    int RegisteredCount { get; }
    IReadOnlyCollection<string> RegisteredTypeNames { get; }
    int Generation { get; }
    bool IsLoaded { get; }

    IComponentLifetime CreateLifetime();
    void Initialize();
    void Register(string name, Func<Component> factory);
    void RegisterLifecycle(string name, ScriptLifecycle callbacks);
    int Create(ObjectHandle owner, string name);
    bool Destroy(int instanceId);
    Component? Find(int instanceId);
    void ClearRegistrations();

    void Add(Component component);
    void Remove(Component component);
    IReadOnlyList<Component> FindComponents(ObjectHandle owner);
    bool DispatchLifecycle(int instanceId, int phase);
    bool DispatchEnabled(int instanceId, bool enabled);
    void ApplyEnabled(Component component, bool enabled);
    void DispatchMessage(Component component, string message);
    void FlushRegistrations();
    void PrePhysicsTick(float dt);
    void UpdateTick(float dt);
    void PostPhysicsTick(float dt);
    int SweepOrphans();
    void Clear();

    void InstallSynchronizationContext();
    int DrainContinuations();
    int DropContinuations();
    Assembly[] LoadedAssemblies();
    bool Load(string path);
    int Reload();
    void Unload();
    bool IsPreviousContextAlive();

    void RegisterAnimation(string name, Func<AniBehavior> factory);
    bool AnimationExists(string name);
    int AnimationCount { get; }
    IReadOnlyCollection<string> AnimationNames { get; }
    int CreateAnimation(string name);
    bool DestroyAnimation(int instanceId);
    AniBehavior? FindAnimation(int instanceId);
    void DispatchAnimation(AniEvent value);
    void ClearAnimations();

    void RegisterNode(string name, int kind, Func<BTNode> factory);
    bool NodeExists(string name);
    int NodeCount { get; }
    BTNode? CreateNode(string name);
    IEnumerable<string> NodeNames(BTNodeKind kind);
    void ClearNodes();

    int TreeCount { get; }
    int AddTree(BTNode root, Entity owner, BlackBoard board);
    bool DestroyTree(int instanceId);
    void TickTree(int instanceId, float dt);
    BlackBoard? GetTreeBlackBoard(int instanceId);
    int SweepTreeOrphans();
    void ClearTrees();
    BTStats GetTreeStats();
    void ResetTreeStats();

    InvokeOutcome Invoke(string typeName, string methodName, string[] args, out string payload);
    int CollectNow();
    int SetLatencyMode(int lowLatency);
    GcStats ReadGcStats();
}

internal interface IContactRuntime
{
    Component? Current { get; set; }
    void Suspend(Component subscriber);
    void BindRole(ObjectHandle owner, ulong body, uint shape, ShapeRole role, ComponentLifetime lifetime);
    ContactStream Observe(Component subscriber, ObjectHandle owner, ShapeRole self, ShapeRole other,
        ContactPhases phases, int capacity, ContactGrouping grouping);
    void Remove(ContactStream stream);
    void Route(NativeContact value);
    void EndUpdate();
    void EndFrame();
}

internal static class ManagedRuntime
{
    private static readonly Lazy<IManagedRuntime> Runtime = new(LoadImplementation);
    internal static IManagedRuntime Current => Runtime.Value;

    [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "The process-resident F# runtime is shipped explicitly; this collectible hosting path does not support NativeAOT.")]
    [UnconditionalSuppressMessage("Trimming", "IL2072", Justification = "The shipped F# Runtime has a public parameterless constructor; this hosting path is not trimmed.")]
    private static IManagedRuntime LoadImplementation()
    {
        // hostfxr can put ScriptCore in its own component load context. Loading
        // the implementation in Default would duplicate the contract assembly.
        // Resolve it in the same resident context as the native binding types.
        string directory = Path.GetDirectoryName(typeof(Component).Assembly.Location)!;
        var context = System.Runtime.Loader.AssemblyLoadContext.GetLoadContext(typeof(Component).Assembly)!;
        context.LoadFromAssemblyPath(
            Path.Combine(directory, "FSharp.Core.dll"));
        Assembly assembly = context.LoadFromAssemblyPath(
            Path.Combine(directory, "CreatorEngine.ManagedRuntime.dll"));
        Type type = assembly.GetType("CreatorEngine.ManagedRuntime.Runtime", throwOnError: true)!;
        var implementation = (IManagedRuntime)Activator.CreateInstance(type)!;
        if (implementation.ScriptApi != Native.ExpectedVersion)
        {
            throw new InvalidOperationException("The F# managed runtime and native binding script API versions differ.");
        }

        return implementation;
    }
}
