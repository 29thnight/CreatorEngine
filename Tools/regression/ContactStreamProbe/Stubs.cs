namespace CreatorEngine;

public readonly record struct ObjectHandle(uint Index, uint Generation) { public bool IsValid => Generation != 0; }
public readonly record struct Entity(ObjectHandle Handle);
public struct Float3 { public float X, Y, Z; }
public sealed class Component
{
    private bool _enabled = true;
    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (_enabled == value) return;
            _enabled = value;
            if (!value) ContactRouter.Suspend(this);
        }
    }

    public bool BeginSucceeded = true;
    public SimulationScope Scope { get; } = new();
}
public sealed class SimulationScope
{
    private readonly List<Action> _cleanup = new();
    public void RegisterCleanup(Action cleanup) => _cleanup.Add(cleanup);
    public void Cancel() { foreach (var action in _cleanup) action(); _cleanup.Clear(); }
}
