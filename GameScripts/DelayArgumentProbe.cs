namespace CreatorEngine.Scripts;

// Historical fixture identity retained for scene compatibility. Dedicated delay API was removed.
public sealed partial class DelayArgumentProbe : Component
{
    public void OnBeginSimulation() => Log("[RETIRED] dedicated delay fixture; use FixedUpdateContractProbe");
}
