namespace CreatorEngine.Scripts;

// Standard C# Task ownership: cancellation is cooperative and belongs to this script.
public sealed partial class DrainReentryProbe : Component
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
            for (int i = 0; i < 10; ++i)
            {
                await Task.Yield();
                token.ThrowIfCancellationRequested();
                Log($"[LC0] kind=drain point=yield frame={FrameCount} tid={Environment.CurrentManagedThreadId}");
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Log($"[CSharpTask] {ex}"); }
    }
}
