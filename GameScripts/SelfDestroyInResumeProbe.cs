namespace CreatorEngine.Scripts;

public sealed partial class SelfDestroyInResumeProbe : Component
{
    private float _elapsed;
    private bool _acted;

    private const string Kind = "resume";

    private void Mark(string point)
        => Log($"[LC0] kind={Kind} point={point} frame={FrameCount} tid={Environment.CurrentManagedThreadId}");

    public void OnInitialized()       => Mark("init");
    public void OnAddedToScene()      => Mark("added");
    public void OnEnable()            => Mark("enable");
    public void OnBeginSimulation()   => Mark("beginhook");
    public void OnDisable()           => Mark("disable");
    public void OnEndSimulation()     => Mark("end");
    public void OnRemovingFromScene() => Mark("removing");
    public void OnUninitializing()    => Mark("uninit");

    private int _ticks;
    public void PrePhysics(float tick)
    {
        if (_ticks++ < 6) Mark("pre");
    }

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
