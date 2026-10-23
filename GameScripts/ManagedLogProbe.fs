namespace CreatorEngine.Scripts

open CreatorEngine

type ManagedLogProbe() =
    inherit Component()

    [<EngineCallable>]
    static member EmitLogLines() =
        Component.Log("[ManagedLogProbe] info 한 줄")
        Component.LogWarning("[ManagedLogProbe] warn 한 줄")
        "{\"emitted\":2}"
