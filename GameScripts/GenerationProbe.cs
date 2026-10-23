namespace CreatorEngine.Scripts;

public sealed partial class GenerationProbe : Component
{
    private int _updates;

    private static int _created;
    private static int _recordedHooks;
    [EngineCallable] public static int RecordedHooks() => _recordedHooks;

    private readonly int _id;

    public GenerationProbe() => _id = ++_created;

    private void Mark(string point)
    {
        ++_recordedHooks;
        Log($"[LC7] id={_id} point={point} frame={FrameCount}");
    }

    public void OnInitialized()       => Mark("init");
    public void OnAddedToScene()      => Mark("added");
    public void OnEnable()            => Mark("enable");
    public void OnBeginSimulation()   => Mark("begin");
    public void OnDisable()           => Mark("disable");
    public void OnEndSimulation()     => Mark("end");
    public void OnRemovingFromScene() => Mark("removing");
    public void OnUninitializing()    => Mark("uninit");

    public void OnTickSimulate(float dt)
    {
        if (_updates++ == 0) Mark("update");
        if (dt <= 0 || Math.Abs(dt - 1f / 60f) > 1e-6f) throw new InvalidOperationException("Wrong fixed dt");
    }
}
