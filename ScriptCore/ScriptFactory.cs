namespace CreatorEngine;

// Binding facade only. Factory and instance storage are owned by the F# runtime.
internal static class ScriptFactory
{
    public static void Initialize() => ManagedRuntime.Current.Initialize();
    public static void Register(string name, Func<Component> factory) => ManagedRuntime.Current.Register(name, factory);
    public static void RegisterLifecycle(string name, ScriptLifecycle callbacks) => ManagedRuntime.Current.RegisterLifecycle(name, callbacks);
    public static void ClearRegistrations() => ManagedRuntime.Current.ClearRegistrations();
    public static int RegisteredCount => ManagedRuntime.Current.RegisteredCount;
    public static IReadOnlyCollection<string> RegisteredTypeNames => ManagedRuntime.Current.RegisteredTypeNames;
    public static int Create(ObjectHandle owner, string name) => ManagedRuntime.Current.Create(owner, name);
    public static bool Destroy(int instanceId) => ManagedRuntime.Current.Destroy(instanceId);
    public static Component? Find(int instanceId) => ManagedRuntime.Current.Find(instanceId);
}
