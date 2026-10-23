namespace CreatorEngine.Scripts;

public sealed partial class DdolWaitProbe : Component
{
    private int _updates;
    private float _elapsed;
    private bool _reported;

    private const float CrossSeconds = 3.0f;

    private bool _dtReported;

    private void Mark(string point) => Log($"[LC7c] point={point} frame={FrameCount}");

    public void PostPhysics(float tick)
    {
        if (_dtReported) return;
        _dtReported = true;
        Log($"[LC7c] point=dt value={tick:0.#####} cross={CrossSeconds} frames≈{(tick > 0f ? CrossSeconds / tick : -1f):0}");
    }

    public void OnInitialized()       => Mark("init");
    public void OnAddedToScene()      => Mark("added");
    public void OnBeginSimulation()   => Mark("begin");
    public void OnRemovingFromScene() => Mark("removing");
    public void OnEndSimulation()     => Mark("end");
    public void OnUninitializing()    => Mark("uninit");

    public void OnTickSimulate(float dt)
    {
        if (_updates++ == 0) Mark("update");
        _elapsed += dt;
        if (!_reported && _elapsed >= CrossSeconds)
        {
            _reported = true;
            Mark($"resumed name={Entity.Name}");
        }
    }
}
