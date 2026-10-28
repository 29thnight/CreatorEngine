namespace CreatorEngine.Native

open System
open System.Runtime
open System.Runtime.InteropServices

[<Struct; StructLayout(LayoutKind.Sequential)>]
type GcStats =
    val mutable Gen0Collections: int
    val mutable Gen1Collections: int
    val mutable Gen2Collections: int
    val mutable HeapSizeBytes: int64
    val mutable TotalAllocatedBytes: int64
    val mutable FragmentedBytes: int64
    val mutable PauseTimePercentage: int64

type GcControl =
    [<UnmanagedCallersOnly>]
    static member CollectNow() : int =
        try
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking = true, compacting = true)
            GC.WaitForPendingFinalizers()
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking = true, compacting = true)
            0
        with _ -> -1

    [<UnmanagedCallersOnly>]
    static member SetLatencyMode(lowLatency: int) : int =
        try
            GCSettings.LatencyMode <- if lowLatency <> 0 then GCLatencyMode.SustainedLowLatency else GCLatencyMode.Interactive
            0
        with _ -> -1

    [<UnmanagedCallersOnly>]
    static member GetStats(output: nativeint) : int =
        if output = 0n then -1
        else
            try
                let info = GC.GetGCMemoryInfo()
                let mutable stats = GcStats()
                stats.Gen0Collections <- GC.CollectionCount(0)
                stats.Gen1Collections <- GC.CollectionCount(1)
                stats.Gen2Collections <- GC.CollectionCount(2)
                stats.HeapSizeBytes <- info.HeapSizeBytes
                stats.TotalAllocatedBytes <- GC.GetTotalAllocatedBytes(false)
                stats.FragmentedBytes <- info.FragmentedBytes
                stats.PauseTimePercentage <- int64 (info.PauseTimePercentage * 100.0)
                Marshal.StructureToPtr(stats, output, false)
                0
            with _ -> -1
