namespace CreatorEngine.ManagedRuntime

open CreatorEngine

/// Process-resident engine service. Collectible callbacks are owned by Registry.
type Runtime() =
    let continuations = Continuations()
    let registry = Registry(continuations)
    let loader = Loader(registry)
    let auxiliary = AuxiliaryFactories()
    let contacts = Contacts() :> IContactRuntime
    let trees = Trees(fun () -> auxiliary.NodeCount)

    interface IManagedRuntime with
        member _.ScriptApi = 45
        member _.Contacts = contacts
        member _.ActiveCount = registry.ActiveCount
        member _.InFixedUpdate = registry.InFixedUpdate
        member _.RegisteredCount = registry.RegisteredCount
        member _.RegisteredTypeNames = registry.RegisteredTypeNames
        member _.Generation = loader.Generation
        member _.IsLoaded = loader.IsLoaded
        member _.CreateLifetime() = Lifetime() :> IComponentLifetime
        member _.Initialize() = registry.Initialize()
        member _.Register(name, factory) = registry.Register(name, factory)

        member _.RegisterLifecycle(name, callbacks) =
            registry.RegisterLifecycle(name, callbacks)

        member _.Create(owner, name) = registry.Create(owner, name)
        member _.Destroy(instanceId) = registry.Destroy(instanceId)
        member _.Find(instanceId) = registry.Find(instanceId)
        member _.ClearRegistrations() = registry.ClearRegistrations()
        member _.Add(script) = registry.Add(script)
        member _.Remove(script) = registry.Remove(script)
        member _.FindComponents(owner) = registry.FindComponents(owner)

        member _.DispatchLifecycle(instanceId, phase) =
            registry.DispatchLifecycle(instanceId, phase)

        member _.DispatchEnabled(instanceId, enabled) =
            registry.DispatchEnabled(instanceId, enabled)

        member _.ApplyEnabled(script, enabled) = registry.ApplyEnabled(script, enabled)

        member _.DispatchMessage(script, message) =
            registry.DispatchMessage(script, message)

        member _.FlushRegistrations() = registry.FlushRegistrations()
        member _.PrePhysicsTick(dt) = registry.PrePhysicsTick(dt)
        member _.UpdateTick(dt) = registry.UpdateTick(dt)
        member _.PostPhysicsTick(dt) = registry.PostPhysicsTick(dt)
        member _.SweepOrphans() = registry.SweepOrphans()
        member _.Clear() = registry.Clear()
        member _.InstallSynchronizationContext() = continuations.Install()
        member _.DrainContinuations() = continuations.Drain()
        member _.DropContinuations() = continuations.DropPending()
        member _.LoadedAssemblies() = loader.LoadedAssemblies()
        member _.Load(path) = loader.Load(path)
        member _.Reload() = loader.Reload()
        member _.Unload() = loader.Unload()
        member _.IsPreviousContextAlive() = loader.IsPreviousContextAlive()

        member _.RegisterAnimation(name, factory) =
            auxiliary.RegisterAnimation(name, factory)

        member _.AnimationExists(name) = auxiliary.AnimationExists(name)
        member _.AnimationCount = auxiliary.AnimationCount
        member _.AnimationNames = auxiliary.AnimationNames
        member _.CreateAnimation(name) = auxiliary.CreateAnimation(name)
        member _.DestroyAnimation(instanceId) = auxiliary.DestroyAnimation(instanceId)
        member _.FindAnimation(instanceId) = auxiliary.FindAnimation(instanceId)
        member _.DispatchAnimation(value) = auxiliary.DispatchAnimation(value)
        member _.ClearAnimations() = auxiliary.ClearAnimations()

        member _.RegisterNode(name, kind, factory) =
            auxiliary.RegisterNode(name, kind, factory)

        member _.NodeExists(name) = auxiliary.NodeExists(name)
        member _.NodeCount = auxiliary.NodeCount
        member _.CreateNode(name) = auxiliary.CreateNode(name)
        member _.NodeNames(kind) = auxiliary.NodeNames(kind)
        member _.ClearNodes() = auxiliary.ClearNodes()
        member _.TreeCount = trees.Count
        member _.AddTree(root, owner, board) = trees.Add(root, owner, board)
        member _.DestroyTree(instanceId) = trees.Destroy(instanceId)
        member _.TickTree(instanceId, dt) = trees.Tick(instanceId, dt)
        member _.GetTreeBlackBoard(instanceId) = trees.GetBlackBoard(instanceId)
        member _.SweepTreeOrphans() = trees.SweepOrphans()
        member _.ClearTrees() = trees.Clear()
        member _.GetTreeStats() = trees.GetStats()
        member _.ResetTreeStats() = trees.ResetStats()

        member _.Invoke(typeName, methodName, args, payload) =
            let struct (result, text) =
                Callable.invoke (loader.LoadedAssemblies()) typeName methodName args

            payload <- text
            result

        member _.CollectNow() = GarbageCollection.collectNow ()

        member _.SetLatencyMode(lowLatency) =
            GarbageCollection.setLatencyMode lowLatency

        member _.ReadGcStats() = GarbageCollection.readStats ()
