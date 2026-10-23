namespace CreatorEngine.Scripts;

public sealed partial class FailBeginProbe : Component
{
    private const string Kind = "failbegin";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public void OnInitialized()       => Mark("OnInitialized");
    public void OnAddedToScene()      => Mark("OnAddedToScene");
    public void OnEnable()            => Mark("OnEnable");

    public void OnBeginSimulation()
    {
        Mark("OnBeginSimulation");
        throw new InvalidOperationException("[LC1] 의도한 실패 — 시작 훅이 준비를 마치지 못했다");
    }

    public void OnDisable()           => Mark("OnDisable");
    public void OnEndSimulation()     => Mark("OnEndSimulation");
    public void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public void OnUninitializing()    => Mark("OnUninitializing");

    public void OnTickSimulate(float dt) => Mark("OnTickSimulate");
}
