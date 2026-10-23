namespace CreatorEngine;

// Native animation ABI facade; factories and instances are owned by F#.
internal static class AniBehaviorFactory
{
    public static void Register(string name, Func<AniBehavior> factory) => ManagedRuntime.Current.RegisterAnimation(name, factory);
    public static bool Exists(string name) => ManagedRuntime.Current.AnimationExists(name);
    public static int RegisteredCount => ManagedRuntime.Current.AnimationCount;
    public static IReadOnlyCollection<string> RegisteredTypeNames => ManagedRuntime.Current.AnimationNames;
    public static void ClearRegistrations() => ManagedRuntime.Current.ClearAnimations();
    public static int Create(string name) => ManagedRuntime.Current.CreateAnimation(name);
    public static bool Destroy(int instanceId) => ManagedRuntime.Current.DestroyAnimation(instanceId);
    public static AniBehavior? Find(int instanceId) => ManagedRuntime.Current.FindAnimation(instanceId);
    public static void Dispatch(in AniEvent value) => ManagedRuntime.Current.DispatchAnimation(value);
}
