namespace CreatorEngine.Scripts

open System
open System.Threading
open System.Threading.Tasks
open CreatorEngine

type WorkerCallProbe() =
    inherit Component()

    let stop = new CancellationTokenSource()
    let mutable work: Task = Task.CompletedTask
    static member private WriteLog(message: string) = Component.Log(message)

    member private this.Run(token: CancellationToken) =
        task {
            try
                let self = this.Entity

                WorkerCallProbe.WriteLog(
                    $"[LC5c] where=game tid={Environment.CurrentManagedThreadId} alive={self.IsAlive} name='{self.Name}'"
                )

                do!
                    Task.Run(
                        Action(fun () ->
                            WorkerCallProbe.WriteLog(
                                $"[LC5c] where=worker tid={Environment.CurrentManagedThreadId} alive={self.IsAlive} name='{self.Name}'"
                            )),
                        token
                    )
            with
            | :? OperationCanceledException -> ()
            | error -> WorkerCallProbe.WriteLog($"[CSharpTask] {error}")
        }

    member this.OnBeginSimulation() : unit = work <- this.Run(stop.Token)
    member _.OnEndSimulation() : unit = stop.Cancel()

    member _.OnUninitializing() : unit =
        stop.Cancel()
        stop.Dispose()
