namespace CreatorEngine.Scripts;

// Standard C# Task ownership: cancellation is cooperative and belongs to this script.
public sealed partial class WorkerCallProbe : Component
{
    private readonly CancellationTokenSource _stop = new();
    private Task? _work;
    public void OnBeginSimulation() { _work = Run(_stop.Token); }
    public void OnEndSimulation() { _stop.Cancel(); }
    public void OnUninitializing() { _stop.Cancel(); _stop.Dispose(); }
    private async Task Run(CancellationToken token)
    {
        try
        {
            var self = Entity;
            Log($"[LC5c] where=game tid={Environment.CurrentManagedThreadId} alive={self.IsAlive} name='{self.Name}'");
            await Task.Run(() => Log($"[LC5c] where=worker tid={Environment.CurrentManagedThreadId} alive={self.IsAlive} name='{self.Name}'"), token);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Log($"[CSharpTask] {ex}"); }
    }
}
