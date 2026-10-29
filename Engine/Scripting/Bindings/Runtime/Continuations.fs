namespace CreatorEngine

open System
open System.Collections.Concurrent
open System.Threading
open CreatorEngine

/// Presentation-frame dispatch for ordinary .NET await, independent of fixed time.
type internal Continuations() as this =
    inherit SynchronizationContext()

    let queue = ConcurrentQueue<struct (SendOrPostCallback * obj)>()
    let mutable previous: SynchronizationContext = null

    member _.Install() =
        previous <- SynchronizationContext.Current
        SynchronizationContext.SetSynchronizationContext(this)

    member _.Uninstall() =
        if obj.ReferenceEquals(SynchronizationContext.Current, this) then
            SynchronizationContext.SetSynchronizationContext(previous)
        previous <- null

    override _.Post(callback, state) = queue.Enqueue(struct (callback, state))

    override _.Send(callback, state) =
        if not NativeApi.IsGameThread then
            raise (
                NotSupportedException("SynchronizationContext.Send requires the game thread; await the result instead.")
            )

        callback.Invoke(state)

    override _.CreateCopy() = this

    member _.Drain() =
        if not NativeApi.IsGameThread then
            NativeApi.Log(3, "[GameThread] Continuation drain requires the game thread.")
            0
        else
            let budget = queue.Count
            let mutable ran = 0
            let mutable item = Unchecked.defaultof<struct (SendOrPostCallback * obj)>

            while ran < budget && queue.TryDequeue(&item) do
                ran <- ran + 1
                let struct (callback, state) = item

                try
                    callback.Invoke(state)
                with error ->
                    NativeApi.Log(3, $"[GameThread] continuation failed: {error}")

            ran

    member _.DropPending() =
        let mutable dropped = 0
        let mutable item = Unchecked.defaultof<struct (SendOrPostCallback * obj)>

        while queue.TryDequeue(&item) do
            dropped <- dropped + 1

        dropped
