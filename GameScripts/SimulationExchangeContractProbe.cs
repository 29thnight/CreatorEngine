using System.Runtime.InteropServices;
namespace CreatorEngine.Scripts;

public sealed partial class SimulationExchangeContractProbe : Component
{
    private static int _completed, _failures, _ended;
    private static ulong _epoch;
    private static SimulationExchangeContractProbe? _driver;
    private bool _done, _pausedQueued;
    private ulong _pausedTick;
    private int _updates;
    private static void Require(bool value, string reason)
    {
        if (value) return;
        ++_failures;
        throw new InvalidOperationException("[N3_FAIL] " + reason);
    }
    [EngineCallable] public static string State()
    {
        Simulation.GetStatus(out var s);
        return $"{{\"completed\":{_completed},\"failures\":{_failures},\"ended\":{_ended},\"epoch\":{s.Epoch},\"committed\":{s.Committed},\"executing\":{s.Executing},\"queued\":{s.Queued},\"active\":{s.Active}}}";
    }
    [EngineCallable] public static bool QueuePaused()
    {
        Require(_driver is not null, "driver missing");
        Require(Simulation.GetStatus(out var s) == SimulationResult.Ok && s.Executing == 0, "pause status");
        _driver!._pausedTick = s.NextTick;
        _driver._pausedQueued = true;
        Require(Simulation.SetWorldPosition(s.Epoch, s.NextTick, _driver.Entity, new(70,0,0), out _) == SimulationResult.Ok, "paused admission");
        return true;
    }
    [EngineCallable] public static bool CheckStopped()
    {
        Require(Simulation.GetStatus(out var s) == SimulationResult.Ok && s.Active == 0, "stopped inactive");
        Require(Simulation.Submit(_epoch, 1, 1, [], out _) == SimulationResult.StaleEpoch, "stop invalidation");
        return true;
    }
    public void OnInitialized() => Console.WriteLine("[SMOKE] managed OnInitialized: SimulationExchangeContractProbe");
    public void OnBeginSimulation()
    {
        Console.WriteLine("[SMOKE] managed OnBeginSimulation: SimulationExchangeContractProbe");
        _driver = this;
        Require(Simulation.GetStatus(out var s) == SimulationResult.Ok && s.Active == 1 && s.Committed == 0, "new play epoch");
        _epoch = s.Epoch;
        Require(Simulation.SetWorldPosition(_epoch, 2, Entity, new(10,0,0), out var first) == SimulationResult.Ok, "first admission");
        Require(Simulation.SetWorldPosition(_epoch, 2, Entity, new(20,0,0), out var second) == SimulationResult.Ok && second > first, "FIFO admission");
        Require(Simulation.SetWorldPosition(_epoch, 2, ObjectHandle.Invalid, new(99,0,0), out _) == SimulationResult.Ok, "dead target admitted then validated at tick");
        Require(Simulation.Submit(_epoch, 2, 999, [], out _) == SimulationResult.Ok, "unknown command receipt");
        Require(Simulation.SetWorldPosition(_epoch, 2, Entity, new(float.NaN,0,0), out _) == SimulationResult.Ok, "nonfinite value validated at tick");
        Require(Simulation.Submit(_epoch - 1, 2, 1, [], out _) == SimulationResult.StaleEpoch, "stale admission");
        Require(Simulation.Submit(_epoch, 0, 1, [], out _) == SimulationResult.Late, "late admission");
        Require(Simulation.Submit(_epoch, 129, 1, [], out _) == SimulationResult.TooFar, "bounded horizon");
        Require(Simulation.Submit(_epoch, 3, 1, new byte[65], out _) == SimulationResult.InvalidArgument, "bounded payload");
    }
    public void OnTickSimulate(float dt)
    {
        ++_updates;
        Require(Simulation.GetStatus(out var s) == SimulationResult.Ok && s.Epoch == _epoch && s.Executing == (ulong)_updates && s.Committed + 1 == s.Executing, "execution/commit order");
        Require(Simulation.Submit(_epoch, s.Executing, 1, [], out _) == SimulationResult.Late, "no reentry into current batch");
        if (_updates == 1)
        {
            Require(Math.Abs(Entity.Transform!.WorldPosition.X) < .001f, "future command ran early");
            Require(Simulation.SetWorldPosition(_epoch, 4, Entity, new(40,0,0), out _) == SimulationResult.Ok, "update submits next batch");
            Require(Simulation.ReadSnapshot(_epoch, 0, [], out _) == SimulationResult.NoSnapshot, "no uncommitted snapshot");
        }
        if (_updates == 2) Require(Math.Abs(Entity.Transform!.WorldPosition.X - 20) < .001f, "FIFO commands before update");
        if (_updates == 3)
        {
            Span<byte> output = stackalloc byte[24];
            Require(Simulation.ReadSnapshot(_epoch, 1, [], out var small) == SimulationResult.BufferTooSmall && small.Bytes == 24, "small read retry");
            Require(Simulation.ReadSnapshot(_epoch, 1, output, out var h) == SimulationResult.Ok && h.Tick == 2 && h.Applied == 2 && h.Rejected == 3 && h.Schema == 1, "committed receipt");
            Require(Math.Abs(MemoryMarshal.Read<float>(output[8..]) - 20) < .001f && MemoryMarshal.Read<uint>(output[20..]) == 1, "post physics value snapshot");
        }
        if (_updates == 4) Require(Math.Abs(Entity.Transform!.WorldPosition.X - 40) < .001f, "future batch position");
        if (_pausedQueued && s.Executing == _pausedTick)
            Require(Math.Abs(Entity.Transform!.WorldPosition.X - 70) < .001f, "paused queue applied on resume");
        if (_updates >= 14 && !_done)
        {
            Span<byte> output = stackalloc byte[4096];
            Require(Simulation.ReadSnapshot(_epoch, 0, output, out var h) == SimulationResult.SnapshotGap && h.Tick == s.Committed - 7, "bounded snapshot loss signal");
            _done = true; ++_completed;
            Console.WriteLine($"[N3_PRODUCT_OK] completed={_completed} epoch={_epoch} tick={s.Executing} applied=FIFO snapshot=postphysics gap=explicit");
        }
    }
    public void OnEndSimulation() { ++_ended; _driver = null; }
}
