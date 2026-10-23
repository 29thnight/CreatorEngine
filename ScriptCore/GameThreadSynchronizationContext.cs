namespace CreatorEngine;

// ABI/bootstrap compatibility only; queue ownership and dispatch are in F#.
internal sealed class GameThreadSynchronizationContext
{
    internal static GameThreadSynchronizationContext? Installed { get; private set; }

    internal static void Install()
    {
        ManagedRuntime.Current.InstallSynchronizationContext();
        Installed = new GameThreadSynchronizationContext();
    }

    internal int Drain() => ManagedRuntime.Current.DrainContinuations();
    internal int DropPending() => ManagedRuntime.Current.DropContinuations();
}
