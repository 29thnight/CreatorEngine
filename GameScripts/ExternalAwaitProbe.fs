namespace CreatorEngine.Scripts

open System
open System.Threading
open System.Threading.Tasks
open CreatorEngine

type ExternalAwaitProbe() =
    inherit Component()

    let stop = new CancellationTokenSource()
    let mutable work: Task = Task.CompletedTask
    static member private WriteLog(message: string) = Component.Log(message)

    member private this.Run(token: CancellationToken) =
        task {
            try
                do!
                    Task.Run(
                        Func<Task>(fun () ->
                            task {
                                do! Task.Delay(50, token)

                                ExternalAwaitProbe.WriteLog(
                                    $"[LC5b] point=worker tid={Environment.CurrentManagedThreadId} owner=-"
                                )
                            }),
                        token
                    )

                token.ThrowIfCancellationRequested()

                if this.IsAlive then
                    ExternalAwaitProbe.WriteLog(
                        $"[LC5b] point=external tid={Environment.CurrentManagedThreadId} owner={this.Entity.Name}"
                    )
            with
            | :? OperationCanceledException -> ()
            | error -> ExternalAwaitProbe.WriteLog($"[CSharpTask] {error}")
        }

    member this.OnBeginSimulation() : unit =
        ExternalAwaitProbe.WriteLog(
            $"[LC5b] point=begin tid={Environment.CurrentManagedThreadId} owner={this.Entity.Name}"
        )

        work <- this.Run(stop.Token)

    member _.OnEndSimulation() : unit = stop.Cancel()

    member _.OnUninitializing() : unit =
        stop.Cancel()
        stop.Dispose()
