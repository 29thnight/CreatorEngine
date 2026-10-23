namespace CreatorEngine.ManagedRuntime

open System
open CreatorEngine

/// Internal engine subscriptions only. Authored tasks own their cancellation.
type internal Lifetime() =
    let cleanup = ResizeArray<Action>()
    let mutable ended = false

    let run (action: Action) =
        try
            action.Invoke()
        with error ->
            Native.Log(3, $"[ComponentLifetime] cleanup failed: {error}")

    interface IComponentLifetime with
        member _.RegisterCleanup(action) =
            if ended then run action else cleanup.Add(action)

        member _.End() =
            if not ended then
                ended <- true

                // Detach before callbacks: cleanup may reenter End or register more.
                let retiring = cleanup.ToArray()
                cleanup.Clear()

                for action in retiring do
                    run action
