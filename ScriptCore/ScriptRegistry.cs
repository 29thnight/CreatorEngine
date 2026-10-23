namespace CreatorEngine;

// Native binding facade. All lifecycle and membership decisions live in F#.
internal static class ScriptRegistry
{
    public enum LifecyclePhase
    {
        OnInitialized = 0,
        OnAddedToScene = 1,
        OnBeginSimulation = 2,
        OnEndSimulation = 3,
        OnRemovingFromScene = 4,
        OnUninitializing = 5,
    }

    public static int ActiveCount => ManagedRuntime.Current.ActiveCount;
    internal static bool InFixedUpdate => ManagedRuntime.Current.InFixedUpdate;
    public static void Add(Component component) => ManagedRuntime.Current.Add(component);
    public static void Remove(Component component) => ManagedRuntime.Current.Remove(component);
    public static bool DispatchLifecycle(int instanceId, int phase) => ManagedRuntime.Current.DispatchLifecycle(instanceId, phase);
    public static bool DispatchEnabled(int instanceId, bool enabled) => ManagedRuntime.Current.DispatchEnabled(instanceId, enabled);
    internal static void ApplyEnabled(Component component, bool enabled) => ManagedRuntime.Current.ApplyEnabled(component, enabled);
    public static void DispatchMessage(Component target, string message) => ManagedRuntime.Current.DispatchMessage(target, message);
    public static void FlushRegistrations() => ManagedRuntime.Current.FlushRegistrations();
    public static void PrePhysicsTick(float dt) => ManagedRuntime.Current.PrePhysicsTick(dt);
    public static void UpdateTick(float dt) => ManagedRuntime.Current.UpdateTick(dt);
    public static void PostPhysicsTick(float dt) => ManagedRuntime.Current.PostPhysicsTick(dt);
    public static int SweepOrphans() => ManagedRuntime.Current.SweepOrphans();
    public static void Clear() => ManagedRuntime.Current.Clear();

    public static T? FindComponent<T>(ObjectHandle owner) where T : Component
    {
        var components = ManagedRuntime.Current.FindComponents(owner);
        for (int i = 0; i < components.Count; ++i)
            if (components[i] is T match && match.IsAlive) return match;
        return null;
    }

    public static List<T> FindAll<T>(ObjectHandle owner) where T : Component
    {
        var result = new List<T>();
        var components = ManagedRuntime.Current.FindComponents(owner);
        for (int i = 0; i < components.Count; ++i)
            if (components[i] is T match && match.IsAlive) result.Add(match);
        return result;
    }
}
