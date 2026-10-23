namespace CreatorEngine.Scripts;

public sealed partial class ReentrantDestroyProbe : Component
{
    private float _elapsed;
    private bool _acted;

    private const string Kind = "destroy";

    private void Mark(string point)
        => Log($"[LC4] kind={Kind} point={point} tid={Environment.CurrentManagedThreadId}");

    public void OnBeginSimulation()   => Mark("begin");
    public void OnEndSimulation()     => Mark("end");
    public void OnRemovingFromScene() => Mark("removing");
    public void OnUninitializing()    => Mark("uninit");

    public void OnTickSimulate(float dt)
    {
        _elapsed += dt;
        if (_acted || _elapsed < .2f) return;
        _acted = true;
        Mark("update");
        Entity.Destroy();
        Mark("acted");
    }
}
