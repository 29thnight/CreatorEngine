namespace CreatorEngine;

// Passive binding to the F# subscription owner. Native wrapper queries do not
// allocate a runtime lifetime unless they actually register an engine cleanup.
internal sealed class ComponentLifetime
{
    private IComponentLifetime? _implementation;
    internal void RegisterCleanup(Action action)
        => (_implementation ??= ManagedRuntime.Current.CreateLifetime()).RegisterCleanup(action);

    internal void End() => (_implementation ??= ManagedRuntime.Current.CreateLifetime()).End();
}
