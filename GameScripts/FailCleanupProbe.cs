namespace CreatorEngine.Scripts;

public sealed partial class FailCleanupProbe : Component
{
    private readonly CancellationTokenSource _stop = new();
    private const string Kind = "failcleanup";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public void OnInitialized()  => Mark("OnInitialized");
    public void OnAddedToScene() => Mark("OnAddedToScene");
    public void OnEnable()       => Mark("OnEnable");

    public void OnBeginSimulation()
    {
        Mark("OnBeginSimulation");

        _stop.Token.Register(() => throw new InvalidOperationException("[LC2] 의도한 실패 — 정리 콜백 A가 던진다"));
        _stop.Token.Register(() => Mark("cleanup1"));
        _stop.Token.Register(() => Mark("cleanup2"));
        _stop.Token.Register(() => throw new InvalidOperationException("[LC2] 의도한 실패 — 정리 콜백 B가 던진다"));
        _stop.Token.Register(() => Mark("cleanup3"));
    }

    public void OnDisable()           => Mark("OnDisable");
    public void OnEndSimulation()
    {
        try { _stop.Cancel(); }
        catch (AggregateException errors) { foreach (var error in errors.InnerExceptions) Log(error.ToString()); }
        Mark("OnEndSimulation");
    }
    public void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public void OnUninitializing() { _stop.Dispose(); Mark("OnUninitializing"); }

    public void OnTickSimulate(float dt) { }
}
