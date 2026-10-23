namespace CreatorEngine.Scripts;

public sealed partial class LifecycleAxisProbe : Component
{
    private int _fixed;
    [EngineCallable] public void SetActive(bool active) => Entity.SetEnabled(active);

    private const int BeatEvery = 30;

    private const float WarmupSeconds = 0.15f;

    private const float ObserveSeconds = 0.15f;

    private static int s_created;

    private readonly int _ordinal = ++s_created;

    private int _pre;
    private int _post;

    private void Emit(string kind, string name)
        => Log($"[Axis] #{_ordinal} kind={kind} name={name} frame={FrameCount} pre={_pre} post={_post}");


    public void OnInitialized()       => Emit("hook", "OnInitialized");
    public void OnAddedToScene()      => Emit("hook", "OnAddedToScene");
    public void OnBeginSimulation()   => Emit("hook", "OnBeginSimulation");
    public void OnEndSimulation()     => Emit("hook", "OnEndSimulation");
    public void OnRemovingFromScene() => Emit("hook", "OnRemovingFromScene");

    public void OnUninitializing()
    {
        Emit("hook", "OnUninitializing");
        Emit("mark", 0 < Entity.Name.Length ? "ownerreachable" : "ownerlost");
    }


    public void OnEnable()  => Emit("hook", "OnEnable");
    public void OnDisable() => Emit("hook", "OnDisable");


    public void PrePhysics(float tick)
    {
        ++_pre;
        if (1 == _pre) Emit("tick", "prefirst");
        else if (0 == _pre % BeatEvery) Emit("tick", "prebeat");
    }

    public void PostPhysics(float tick)
    {
        ++_post;
        if (1 == _post) Emit("tick", "postfirst");
        else if (0 == _post % BeatEvery) Emit("tick", "postbeat");
    }

    public void OnTickSimulate(float dt)
    {
        if (_fixed++ == 0) Emit("tick", "updatefirst");
        else if (_fixed % BeatEvery == 0) Emit("tick", "updatebeat");
    }
}
