namespace CreatorEngine.Scripts

open System
open System.Threading
open CreatorEngine

type FailCleanupProbe() =
    inherit Component()

    let stop = new CancellationTokenSource()

    member private this.Mark(hook: string) =
        Component.Log($"[LC1] kind=failcleanup hook={hook} owner={this.Entity.Name}")

    member this.OnInitialized() : unit = this.Mark("OnInitialized")
    member this.OnAddedToScene() : unit = this.Mark("OnAddedToScene")
    member this.OnEnable() : unit = this.Mark("OnEnable")

    member this.OnBeginSimulation() : unit =
        this.Mark("OnBeginSimulation")

        stop.Token.Register(Action(fun () -> invalidOp "[LC2] 의도한 실패 — 정리 콜백 A가 던진다"))
        |> ignore

        stop.Token.Register(Action(fun () -> this.Mark("cleanup1"))) |> ignore
        stop.Token.Register(Action(fun () -> this.Mark("cleanup2"))) |> ignore

        stop.Token.Register(Action(fun () -> invalidOp "[LC2] 의도한 실패 — 정리 콜백 B가 던진다"))
        |> ignore

        stop.Token.Register(Action(fun () -> this.Mark("cleanup3"))) |> ignore

    member this.OnDisable() : unit = this.Mark("OnDisable")

    member this.OnEndSimulation() : unit =
        try
            stop.Cancel()
        with :? AggregateException as errors ->
            for error in errors.InnerExceptions do
                Component.Log(error.ToString())

        this.Mark("OnEndSimulation")

    member this.OnRemovingFromScene() : unit = this.Mark("OnRemovingFromScene")

    member this.OnUninitializing() : unit =
        stop.Dispose()
        this.Mark("OnUninitializing")
