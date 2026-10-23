namespace CreatorEngine.Scripts

open System
open System.Collections.Generic
open System.Globalization
open CreatorEngine

type N3PacingProbe() =
    inherit Component()

    static let updates = ResizeArray<string>()
    static let mutable failures = 0
    static let mutable minDt = Single.MaxValue
    static let mutable maxDt = 0.0f

    member _.OnBeginSimulation() : unit =
        updates.Clear()
        failures <- 0
        minDt <- Single.MaxValue
        maxDt <- 0.0f

    member _.OnTickSimulate(dt: single) : unit =
        let mutable status = Unchecked.defaultof<SimulationStatus>

        if Simulation.GetStatus(&status) <> SimulationResult.Ok then
            failures <- failures + 1

        if abs (dt - 1.0f / 60.0f) > 1e-7f || status.Executing = 0UL then
            failures <- failures + 1

        minDt <- min minDt dt
        maxDt <- max maxDt dt
        updates.Add($"[{status.Executing},{Component.FrameCount}]")

    [<EngineCallable>]
    static member State() =
        let lo = minDt.ToString("R", CultureInfo.InvariantCulture)
        let hi = maxDt.ToString("R", CultureInfo.InvariantCulture)
        let tail = String.Join(",", updates |> Seq.skip (max 0 (updates.Count - 120)))

        "{\"failures\":"
        + string failures
        + ",\"minDt\":"
        + lo
        + ",\"maxDt\":"
        + hi
        + ",\"updates\":["
        + tail
        + "]}"
