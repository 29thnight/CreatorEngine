namespace CreatorEngine.Diagnostics;

/// <summary>Names a managed CPU region in the engine frame profiler.</summary>
/// <remarks>Keep a marker as a static field and use <c>using var scope = marker.Auto();</c>.</remarks>
public sealed class ProfilerMarker
{
    private readonly string _name;
    private uint _id;

    public ProfilerMarker(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        _name = name;
    }

    public Scope Auto()
    {
        // Registration is lazy: static script fields may initialize before Native.Bind.
        if (_id == 0) _id = Native.RegisterProfilerMarker(_name);
        return new Scope(Native.BeginProfilerMarker(_id));
    }

    public readonly struct Scope : IDisposable
    {
        private readonly bool _started;
        internal Scope(bool started) => _started = started;
        public void Dispose()
        {
            if (_started) Native.EndProfilerMarker();
        }
    }
}
