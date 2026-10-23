namespace CreatorEngine.Scripts

open System
open System.Threading
open System.Threading.Tasks
open CreatorEngine

type DrainReentryProbe() =
    inherit Component()

    let stop = new CancellationTokenSource()
    let mutable work: Task = Task.CompletedTask
    static member private WriteLog(message: string) = Component.Log(message)

    member private _.Run(token: CancellationToken) =
        task {
            try
                for _ in 0..9 do
                    do! Task.Yield()
                    token.ThrowIfCancellationRequested()

                    DrainReentryProbe.WriteLog(
                        $"[LC0] kind=drain point=yield frame={Component.FrameCount} tid={Environment.CurrentManagedThreadId}"
                    )
            with
            | :? OperationCanceledException -> ()
            | error -> DrainReentryProbe.WriteLog($"[CSharpTask] {error}")
        }

    member this.OnBeginSimulation() : unit = work <- this.Run(stop.Token)
    member _.OnEndSimulation() : unit = stop.Cancel()

    member _.OnUninitializing() : unit =
        stop.Cancel()
        stop.Dispose()
