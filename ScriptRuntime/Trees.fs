namespace CreatorEngine.ManagedRuntime

open System.Collections.Generic
open CreatorEngine

/// The native host keeps IDs; running nodes and blackboards belong to this store.
type internal Trees(nodeCount: unit -> int) =
    let trees = Dictionary<int, struct (BTNode * Entity * BlackBoard)>()
    let mutable nextId = 1
    let mutable ticks = 0L
    let mutable skipped = 0L

    member _.Count = trees.Count

    member _.Add(root, owner, board) =
        let instanceId = nextId
        nextId <- nextId + 1
        trees[instanceId] <- struct (root, owner, board)
        instanceId

    member _.Destroy(instanceId) = trees.Remove(instanceId)

    member _.Tick(instanceId, dt) =
        let mutable entry = Unchecked.defaultof<struct (BTNode * Entity * BlackBoard)>

        if trees.TryGetValue(instanceId, &entry) then
            let struct (root, owner, board) = entry

            if owner.IsAlive then
                ticks <- ticks + 1L
                root.Tick(dt, board) |> ignore
            else
                skipped <- skipped + 1L
        else
            skipped <- skipped + 1L

    member _.GetBlackBoard(instanceId) =
        let mutable entry = Unchecked.defaultof<struct (BTNode * Entity * BlackBoard)>

        if trees.TryGetValue(instanceId, &entry) then
            let struct (_, _, board) = entry
            board
        else
            null

    member _.SweepOrphans() =
        let orphans = ResizeArray<int>()

        for entry in trees do
            let struct (_, owner, _) = entry.Value

            if not owner.IsAlive then
                orphans.Add(entry.Key)

        for instanceId in orphans do
            trees.Remove(instanceId) |> ignore

        orphans.Count

    member _.Clear() =
        trees.Clear()
        nextId <- 1

    member _.ResetStats() =
        ticks <- 0L
        skipped <- 0L

    member _.GetStats() =
        BTStats(TreeCount = trees.Count, NodeTypeCount = nodeCount (), TickCount = ticks, SkippedCount = skipped)
