namespace CreatorEngine.Scripts;

public sealed partial class LifecycleProbe : Component
{
    private int _updates;
    private float _elapsed;
    private bool _reported;

    private ulong _initializedFrame;

    public void OnInitialized()
    {
        _initializedFrame = FrameCount;
        Log($"[Probe] Initialized — {Entity.Name} · 엔진프레임 {_initializedFrame}");
    }

    public void OnBeginSimulation()
    {
        Log($"[Probe] BeginSimulation — {Entity.Name} · 엔진프레임 {FrameCount} (Initialized는 {_initializedFrame})");
    }

    public void OnAddedToScene()
    {
        Log($"[Probe] AddedToScene — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public void OnRemovingFromScene()
    {
        Log($"[Probe] RemovingFromScene — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public void OnEndSimulation()
    {
        Log($"[Probe] EndSimulation — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public void OnUninitializing()
    {
        Log($"[Probe] Uninitializing — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public void OnEnable()
    {
        Log($"[Probe] Enable — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public void OnDisable()
    {
        Log($"[Probe] Disable — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public void OnTickSimulate(float dt)
    {
        if (_updates++ == 0) Log($"[Probe] UpdateStart — {Entity.Name} · dt={dt:R}");
        _elapsed += dt;
        if (!_reported && _elapsed >= .2f)
        {
            _reported = true;
            Log($"[Probe] UpdateProgress — {Entity.Name} · updates={_updates}");
        }
    }
}
