namespace CreatorEngine.Scripts;
public sealed partial class N3PacingProbe : Component
{
    private static readonly List<string> _updates = new();
    private static int _failures;
    private static float _minDt, _maxDt;
    public void OnBeginSimulation() { _updates.Clear(); _failures=0; _minDt=float.MaxValue; _maxDt=0; }
    public void OnTickSimulate(float dt)
    {
        if(Simulation.GetStatus(out var s)!=SimulationResult.Ok) ++_failures;
        if(Math.Abs(dt-1f/60f)>1e-7f || s.Executing==0) ++_failures;
        _minDt=Math.Min(_minDt,dt);_maxDt=Math.Max(_maxDt,dt);
        _updates.Add($"[{s.Executing},{FrameCount}]");
    }
    [EngineCallable] public static string State() =>
        $"{{\"failures\":{_failures},\"minDt\":{_minDt.ToString("R",System.Globalization.CultureInfo.InvariantCulture)},\"maxDt\":{_maxDt.ToString("R",System.Globalization.CultureInfo.InvariantCulture)},\"updates\":[{string.Join(",",_updates.TakeLast(120))}]}}";
}
