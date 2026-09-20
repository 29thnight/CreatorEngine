namespace CreatorEngine.Scripts;

// Read by the native animation Commandlet through the normal serialized-field API.
public sealed partial class AnimationPlaybackProbe : Component
{
    private readonly int _creationThread = Environment.CurrentManagedThreadId;
    [SerializeField] private int _count;
    [SerializeField] private int _wrongThread;
    [SerializeField] private string _order = "";

    public void Quarter() => Record("A");
    public void ThreeQuarter() => Record("B");

    private void Record(string key)
    {
        if (Environment.CurrentManagedThreadId != _creationThread) ++_wrongThread;
        ++_count;
        _order += key;
    }
}
