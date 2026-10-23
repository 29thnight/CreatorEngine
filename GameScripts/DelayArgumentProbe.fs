namespace CreatorEngine.Scripts

open CreatorEngine

type DelayArgumentProbe() =
    inherit Component()

    member _.OnBeginSimulation() : unit =
        Component.Log("[RETIRED] dedicated delay fixture; use FixedUpdateContractProbe")
