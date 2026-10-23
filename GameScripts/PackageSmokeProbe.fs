namespace CreatorEngine.Scripts

open System
open CreatorEngine

type PackageSmokeProbe() =
    inherit Component()

    member _.OnInitialized() : unit =
        Console.WriteLine("[SMOKE] managed OnInitialized: PackageSmokeProbe")

    member _.OnBeginSimulation() : unit =
        Console.WriteLine("[SMOKE] managed OnBeginSimulation: PackageSmokeProbe")

        let result =
            {| schemaVersion = 1
               processId = Environment.ProcessId
               event = "package.begin" |}

        Console.WriteLine("[package.lifecycle] " + System.Text.Json.JsonSerializer.Serialize(result))
