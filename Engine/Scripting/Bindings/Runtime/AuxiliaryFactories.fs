namespace CreatorEngine

open System
open System.Collections.Generic
open CreatorEngine

/// Animation state and BT node instances have owners distinct from components.
type internal AuxiliaryFactories() =
    let animationFactories = Dictionary<string, Func<AniBehavior>>()
    let animationInstances = Dictionary<int, AniBehavior>()

    let nodeFactories =
        Dictionary<string, struct (BTNodeKind * Func<BTNode>)>(StringComparer.Ordinal)

    let mutable nextAnimationId = 1

    member _.AnimationCount = animationFactories.Count
    member _.AnimationNames = animationFactories.Keys :> IReadOnlyCollection<string>
    member _.RegisterAnimation(name, factory) = animationFactories[name] <- factory
    member _.AnimationExists(name) = animationFactories.ContainsKey(name)
    member _.DestroyAnimation(instanceId) = animationInstances.Remove(instanceId)

    member _.FindAnimation(instanceId) =
        let mutable animation: AniBehavior = null

        if animationInstances.TryGetValue(instanceId, &animation) then
            animation
        else
            null

    member _.CreateAnimation(name) =
        match animationFactories.TryGetValue(name) with
        | false, _ ->
            NativeApi.Log(3, $"[ScriptRuntime] Unregistered animation: {name}")
            -1
        | true, factory ->
            let instanceId = nextAnimationId
            nextAnimationId <- nextAnimationId + 1
            animationInstances[instanceId] <- factory.Invoke()
            instanceId

    member _.ClearAnimations() =
        animationFactories.Clear()
        animationInstances.Clear()

    member this.DispatchAnimation(value: AniEvent) =
        let animation = this.FindAnimation(value.InstanceId)

        if not (isNull animation) then
            animation.Entity <- Entity(value.Owner)

            animation.Transform <-
                if NativeApi.HasTransform(value.Owner) then
                    Transform(value.Owner)
                else
                    null

            try
                match enum<AniEventKind>(value.Kind) with
                | AniEventKind.Enter -> animation.Enter()
                | AniEventKind.Update -> animation.Update(value.DeltaTime)
                | AniEventKind.Exit -> animation.Exit()
                | _ -> ()
            with error ->
                NativeApi.Log(3, $"[{animation.GetType().Name}] animation {value.Kind} failed.\n{error}")

    member _.NodeCount = nodeFactories.Count

    member _.RegisterNode(name, kind, factory) =
        nodeFactories[name] <- struct (enum<BTNodeKind>(kind), factory)

    member _.NodeExists(name) = nodeFactories.ContainsKey(name)
    member _.ClearNodes() = nodeFactories.Clear()

    member _.CreateNode(name) =
        match nodeFactories.TryGetValue(name) with
        | false, _ ->
            NativeApi.Log(3, $"[ScriptRuntime] Unregistered BT node: {name}")
            null
        | true, struct (_, factory) ->
            let node = factory.Invoke()
            node.Name <- name
            node

    member _.NodeNames(kind) =
        if kind = BTNodeKind.None then
            nodeFactories.Keys :> seq<string>
        else
            seq {
                for entry in nodeFactories do
                    let struct (registeredKind, _) = entry.Value

                    if registeredKind = kind then
                        yield entry.Key
            }
