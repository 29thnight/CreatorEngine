namespace CreatorEngine.Scripts

open System.Diagnostics
open CreatorEngine

type EntryGuardBenchProbe() =
    inherit Component()
    let calls = 1_000_000
    let warmupFrames = 30
    let samples = 15
    let mutable frames = 0
    let mutable taken = 0
    let mutable sink = 0UL

    member _.PostPhysics(_tick: single) =
        frames <- frames + 1
        if frames <= warmupFrames then
            let mutable sum = 0UL
            for _ = 1 to 1000 do sum <- sum + Component.FrameCount
            sink <- sink ^^^ sum
        elif taken < samples then
            taken <- taken + 1
            let stopwatch = Stopwatch.StartNew()
            let mutable sum = 0UL
            for _ = 1 to calls do sum <- sum + Component.FrameCount
            stopwatch.Stop()
            let guarded = stopwatch.ElapsedTicks
            stopwatch.Restart()
            let mutable bare = 0UL
            for _ = 1 to calls do bare <- bare + sink
            stopwatch.Stop()
            let bareTicks = stopwatch.ElapsedTicks
            sink <- sum ^^^ bare
            let toNs = 1_000_000_000.0 / float Stopwatch.Frequency
            Component.Log(sprintf "[LC6] sample=%d calls=%d guarded_ns=%.3f bare_ns=%.3f delta_ns=%.3f" taken calls (float guarded * toNs / float calls) (float bareTicks * toNs / float calls) (float (guarded - bareTicks) * toNs / float calls))
