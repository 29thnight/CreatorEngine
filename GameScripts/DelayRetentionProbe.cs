namespace CreatorEngine.Scripts;

// Historical fixture identity retained for scene compatibility. Dedicated delay API was removed.
public sealed partial class DelayRetentionProbe : Component
{
    public void OnBeginSimulation() => Log("[RETIRED] dedicated delay fixture; use FixedUpdateContractProbe");
}
