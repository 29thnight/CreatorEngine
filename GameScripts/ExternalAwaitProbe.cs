namespace CreatorEngine.Scripts;

// Standard C# Task ownership: cancellation is cooperative and belongs to this script.
public sealed partial class ExternalAwaitProbe : Component
{
    private readonly CancellationTokenSource _stop = new();
    private Task? _work;
    public void OnBeginSimulation() { Log($"[LC5b] point=begin tid={Environment.CurrentManagedThreadId} owner={Entity.Name}"); _work = Run(_stop.Token); }
    public void OnEndSimulation() { _stop.Cancel(); }
    public void OnUninitializing() { _stop.Cancel(); _stop.Dispose(); }
    private async Task Run(CancellationToken token)
    {
        try
        {
            await Task.Run(async () =>
            {
                await Task.Delay(50, token);
                Log($"[LC5b] point=worker tid={Environment.CurrentManagedThreadId} owner=-");
            }, token);
            token.ThrowIfCancellationRequested();
            if (IsAlive) Log($"[LC5b] point=external tid={Environment.CurrentManagedThreadId} owner={Entity.Name}");
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Log($"[CSharpTask] {ex}"); }
    }
}
