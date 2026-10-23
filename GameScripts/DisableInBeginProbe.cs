namespace CreatorEngine.Scripts;

public sealed partial class DisableInBeginProbe : Component
{
    private const string Kind = "disablebegin";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public void OnInitialized()       => Mark("OnInitialized");
    public void OnAddedToScene()      => Mark("OnAddedToScene");
    public void OnEnable()            => Mark("OnEnable");

    public void OnBeginSimulation()
    {
        Mark("OnBeginSimulation");

        Enabled = false;
    }

    public void OnDisable()           => Mark("OnDisable");
    public void OnEndSimulation()     => Mark("OnEndSimulation");
    public void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public void OnUninitializing()    => Mark("OnUninitializing");

    public void OnTickSimulate(float dt) => Mark("OnTickSimulate");
}
