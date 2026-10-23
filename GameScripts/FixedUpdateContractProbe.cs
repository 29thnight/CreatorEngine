namespace CreatorEngine.Scripts;

// Real native-host acceptance: no coroutine scheduler or public scope is involved.
public sealed partial class FixedUpdateContractProbe : Component
{
    private static int _completed, _failures, _faultDisabled, _removedEnded, _ended, _driverUpdates;
    private int _updates;
    private bool _done;
    private int _disabledCount;
    private ulong _frame;
    private int _inFrame;
    private int _maxInFrame;

    [EngineCallable] public static string State() =>
        $"{{\"completed\":{_completed},\"failures\":{_failures},\"faultDisabled\":{_faultDisabled},\"removedEnded\":{_removedEnded},\"ended\":{_ended},\"driverUpdates\":{_driverUpdates}}}";

    private static void Require(bool value, string reason)
    {
        if (value) return;
        ++_failures;
        throw new InvalidOperationException(reason);
    }

    public void OnTickSimulate(float dt)
    {
        Require(Math.Abs(dt - 1f / 60f) < 1e-7f, "OnTickSimulate did not receive fixedDelta");
        ++_updates;
        if (_frame != FrameCount) { _frame = FrameCount; _inFrame = 0; }
        _maxInFrame = Math.Max(_maxInFrame, ++_inFrame);
        switch (Entity.Name)
        {
            case "FixedFault":
                if (_updates != 1) ++_failures;
                throw new InvalidOperationException("[fixed-update.injected] isolate this component");
            case "FixedRemove":
                Require(_updates == 1, "destroyed instance updated twice");
                Entity.Destroy();
                return;
            case "FixedDriver":
                ++_driverUpdates;
                var target = Entity.Find("FixedTarget").GetComponent<FixedUpdateContractProbe>();
                Require(target is not null, "target missing");
                if (_updates == 6) { target!.Enabled = false; _disabledCount = target._updates; }
                if (_updates == 9)
                {
                    Require(target!._updates == _disabledCount, "disabled component still updated");
                    target.Enabled = true;
                }
                if (_updates >= 30 && !_done)
                {
                    Require(target!._updates > _disabledCount, "reenabled component did not update");
                    Require(_faultDisabled == _completed + 1, "fault not isolated exactly once");
                    Require(_removedEnded == _completed + 1, "removed component teardown missing");
                    _done = true;
                    ++_completed;
                    Console.WriteLine($"[FIXED_UPDATE_PRODUCT] completed={_completed} updates={_updates} target={target._updates} dt={dt:R} maxPerFrame={_maxInFrame}");
                }
                break;
        }
    }

    public void OnDisable() { if (Entity.Name == "FixedFault") ++_faultDisabled; }
    public void OnEndSimulation()
    {
        ++_ended;
        if (Entity.Name == "FixedRemove") ++_removedEnded;
    }
}
