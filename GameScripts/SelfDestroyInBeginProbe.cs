namespace CreatorEngine.Scripts;

public sealed partial class SelfDestroyInBeginProbe : Component
{
    private const string Kind = "begin";

    private void Mark(string point)
        => Log($"[LC0] kind={Kind} point={point} frame={FrameCount} tid={Environment.CurrentManagedThreadId}");

    public void OnInitialized()       => Mark("init");
    public void OnAddedToScene()      => Mark("added");
    public void OnEnable()            => Mark("enable");
    public void OnDisable()           => Mark("disable");
    public void OnEndSimulation()     => Mark("end");
    public void OnRemovingFromScene() => Mark("removing");
    public void OnUninitializing()    => Mark("uninit");

    public void OnBeginSimulation()
    {
        Mark("beginhook");
        Entity.Destroy();
        Mark("destroyed");
    }

    public void OnTickSimulate(float dt) => Mark("unexpected-update");

    private int _ticks;
    public void PostPhysics(float tick)
    {
        if (_ticks++ < 3) Mark("tick");
    }
}
