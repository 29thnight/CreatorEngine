namespace CreatorEngine.Scripts;

public sealed partial class FailInitializedProbe : Component
{
    private const string Kind = "failinit";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public void OnInitialized()
    {
        Mark("OnInitialized");
        throw new InvalidOperationException("[LC1] 의도한 실패 — 초기화가 자원을 잡지 못했다");
    }

    public void OnAddedToScene()      => Mark("OnAddedToScene");
    public void OnEnable()            => Mark("OnEnable");
    public void OnBeginSimulation()   => Mark("OnBeginSimulation");
    public void OnDisable()           => Mark("OnDisable");
    public void OnEndSimulation()     => Mark("OnEndSimulation");
    public void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public void OnUninitializing()    => Mark("OnUninitializing");

    public void OnTickSimulate(float dt) => Mark("OnTickSimulate");
}
