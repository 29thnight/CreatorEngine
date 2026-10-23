namespace CreatorEngine.Scripts

open System
open CreatorEngine

type AnimationPlaybackProbe() as this =
    inherit Component()

    let creationThread = Environment.CurrentManagedThreadId
    do this._order <- ""

    [<DefaultValue; SerializeField>]
    val mutable public _count: int

    [<DefaultValue; SerializeField>]
    val mutable public _wrongThread: int

    [<DefaultValue; SerializeField>]
    val mutable public _order: string

    member private this.Record(key) =
        if Environment.CurrentManagedThreadId <> creationThread then
            this._wrongThread <- this._wrongThread + 1

        this._count <- this._count + 1
        this._order <- this._order + key

    member this.Quarter() = this.Record("A")
    member this.ThreeQuarter() = this.Record("B")
