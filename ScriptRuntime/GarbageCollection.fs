namespace CreatorEngine.ManagedRuntime

open System
open System.Runtime
open CreatorEngine

module internal GarbageCollection =
    let collectNow () =
        try
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking = true, compacting = true)
            GC.WaitForPendingFinalizers()
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking = true, compacting = true)
            0
        with _ ->
            -1

    let setLatencyMode lowLatency =
        try
            GCSettings.LatencyMode <-
                if lowLatency <> 0 then
                    GCLatencyMode.SustainedLowLatency
                else
                    GCLatencyMode.Interactive

            0
        with _ ->
            -1

    let readStats () =
        let info = GC.GetGCMemoryInfo()

        GcStats(
            Gen0Collections = GC.CollectionCount(0),
            Gen1Collections = GC.CollectionCount(1),
            Gen2Collections = GC.CollectionCount(2),
            HeapSizeBytes = info.HeapSizeBytes,
            TotalAllocatedBytes = GC.GetTotalAllocatedBytes(precise = false),
            FragmentedBytes = info.FragmentedBytes,
            PauseTimePercentage = int64 (info.PauseTimePercentage * 100.0)
        )
