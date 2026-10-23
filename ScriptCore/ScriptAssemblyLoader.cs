using System.Reflection;

namespace CreatorEngine;

// Retains the native result ordinals; the collectible loader is implemented in F#.
internal static class ScriptAssemblyLoader
{
    public enum ReloadOutcome
    {
        Reloaded = 0,
        PreviousKept = 1,
        PreviousLost = 2,
    }

    public static int Generation => ManagedRuntime.Current.Generation;
    public static bool IsLoaded => ManagedRuntime.Current.IsLoaded;
    public static Assembly[] LoadedAssemblies() => ManagedRuntime.Current.LoadedAssemblies();
    public static bool Load(string path) => ManagedRuntime.Current.Load(path);
    public static ReloadOutcome Reload() => (ReloadOutcome)ManagedRuntime.Current.Reload();
    public static void Unload() => ManagedRuntime.Current.Unload();
    public static bool IsPreviousContextAlive() => ManagedRuntime.Current.IsPreviousContextAlive();
}
