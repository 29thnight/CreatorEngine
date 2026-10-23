namespace CreatorEngine.Scripts;

public sealed partial class FailSimulateImmediateProbe : Component
{
    private const string Kind = "failsimimm";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    private int _beats;

    public void PostPhysics(float tick)
    {
        if (0 == ++_beats % 30) Mark("beat");
    }

    public void OnInitialized()       => Mark("OnInitialized");
    public void OnAddedToScene()      => Mark("OnAddedToScene");
    public void OnEnable()            => Mark("OnEnable");
    public void OnBeginSimulation()   => Mark("OnBeginSimulation");
    public void OnDisable()           => Mark("OnDisable");
    public void OnEndSimulation()     => Mark("OnEndSimulation");
    public void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public void OnUninitializing()    => Mark("OnUninitializing");

#pragma warning disable CS1998 // await가 없는 async — 즉시 faulted Task를 만드는 것이 이 픽스처의 목적이다
    public void OnTickSimulate(float dt)
    {
        Mark("OnTickSimulate");
        throw new InvalidOperationException("OnTickSimulate failure isolation probe");
    }
#pragma warning restore CS1998
}
