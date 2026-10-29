namespace CreatorEngine

open System

type internal ComponentLifetime() =
    let cleanup = ResizeArray<Action>()
    let mutable ended = false
    let run (action: Action) =
        try action.Invoke()
        with error -> NativeApi.Log(3, $"[ComponentLifetime] cleanup failed: {error}")
    member _.RegisterCleanup(action: Action) =
        if ended then run action else cleanup.Add(action)
    member _.End() =
        if not ended then
            ended <- true
            let retiring = cleanup.ToArray()
            cleanup.Clear()
            for action in retiring do
                run action
