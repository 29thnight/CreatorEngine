namespace CreatorEngine.ManagedRuntime

open System
open System.Collections.Concurrent
open System.Threading
open CreatorEngine

/// Presentation-frame dispatch for ordinary .NET await, independent of fixed time.
type internal Continuations() as this =
    inherit SynchronizationContext()

    let queue = ConcurrentQueue<struct (SendOrPostCallback * obj)>()

    member _.Install() =
        SynchronizationContext.SetSynchronizationContext(this)

    override _.Post(callback, state) = queue.Enqueue(struct (callback, state))

    override _.Send(callback, state) =
        if not Native.IsGameThread then
            raise (
                NotSupportedException("SynchronizationContext.Send requires the game thread; await the result instead.")
            )

        callback.Invoke(state)

    override _.CreateCopy() = this

    member _.Drain() =
        if not Native.IsGameThread then
            Native.Log(3, "[GameThread] Continuation drain requires the game thread.")
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
                    Native.Log(3, $"[GameThread] continuation failed: {error}")

            ran

    member _.DropPending() =
        let mutable dropped = 0
        let mutable item = Unchecked.defaultof<struct (SendOrPostCallback * obj)>

        while queue.TryDequeue(&item) do
            dropped <- dropped + 1

        dropped
