namespace CreatorEngine.ManagedRuntime

open System
open System.Collections.Generic
open CreatorEngine

type private Phase = ScriptRegistry.LifecyclePhase

/// Owns script factories, instances and deferred tick membership for one engine.
/// Native handles remain opaque generation values supplied by ScriptCore bindings.
type internal Registry(continuations: Continuations) =
    let active = ResizeArray<Component>()
    let pendingAdd = ResizeArray<Component>()
    let pendingRemove = ResizeArray<Component>()
    let byOwner = Dictionary<ObjectHandle, ResizeArray<Component>>()
    let factories = Dictionary<string, Func<Component>>(StringComparer.Ordinal)
    let lifecycle = Dictionary<string, ScriptLifecycle>(StringComparer.Ordinal)
    let instances = Dictionary<int, Component>()
    let emptyComponents = Array.empty<Component> :> IReadOnlyList<Component>
    let emptyLifecycle = ScriptLifecycle()

    let mutable nextInstanceId = 1
    let mutable generation = 0u
    let mutable updating = false
    let mutable flushing = false
    let mutable clearing = false

    member _.ActiveCount = active.Count
    member _.InFixedUpdate = updating
    member _.RegisteredCount = factories.Count
    member _.RegisteredTypeNames = factories.Keys :> IReadOnlyCollection<string>

    member _.Register(name, factory) = factories[name] <- factory
    member _.RegisterLifecycle(name, callbacks) = lifecycle[name] <- callbacks

    member _.Initialize() =
        factories.Clear()
        lifecycle.Clear()
        instances.Clear()
        nextInstanceId <- 1

    member _.ClearRegistrations() =
        factories.Clear()
        lifecycle.Clear()
        instances.Clear()

    member _.Find(instanceId) =
        let mutable script: Component = null

        if instances.TryGetValue(instanceId, &script) then
            script
        else
            null

    member _.FindComponents(owner) =
        let mutable scripts = Unchecked.defaultof<ResizeArray<Component>>

        if byOwner.TryGetValue(owner, &scripts) then
            scripts :> IReadOnlyList<Component>
        else
            emptyComponents

    member _.Add(script: Component) =
        if clearing then
            invalidOp "Cannot register scripts during reload teardown"

        pendingAdd.Add(script)
        let owner = script.Entity.Handle

        let components =
            match byOwner.TryGetValue(owner) with
            | true, components -> components
            | _ ->
                let components = ResizeArray<Component>(2)
                byOwner.Add(owner, components)
                components

        components.Add(script)

    member _.Remove(script: Component) =
        script.MarkDestroyed()
        pendingRemove.Add(script)
        let owner = script.Entity.Handle

        match byOwner.TryGetValue(owner) with
        | true, components ->
            components.Remove(script) |> ignore

            if components.Count = 0 then
                byOwner.Remove(owner) |> ignore
        | _ -> ()

    member this.Create(owner: ObjectHandle, name: string) =
        match factories.TryGetValue(name) with
        | false, _ ->
            Native.Log(3, $"[ScriptCore] Unregistered script: {name}")
            -1
        | true, create ->
            let script = create.Invoke()
            script.Entity <- Entity(owner)

            script.Lifecycle <-
                match lifecycle.TryGetValue(name) with
                | true, callbacks -> callbacks
                | _ -> emptyLifecycle

            let transform =
                if Native.HasTransform(owner) then
                    Transform(OwnerHandle = owner)
                else
                    null

            script.BindTransform(transform)
            let instanceId = nextInstanceId
            nextInstanceId <- nextInstanceId + 1
            script.InstanceId <- instanceId
            instances[instanceId] <- script

            try
                this.Add(script)
                instanceId
            with _ ->
                instances.Remove(instanceId) |> ignore
                script.MarkDestroyed()
                script.Lifetime.End()
                reraise ()

    member this.Destroy(instanceId) =
        match instances.TryGetValue(instanceId) with
        | true, script ->
            instances.Remove(instanceId) |> ignore
            this.Remove(script)
            true
        | _ -> false

    member private _.ReportFailure(script: Component, phase: string, error: exn) =
        Native.Log(3, $"[{script.GetType().Name}] {phase} failed; disabling this script.\n{error}")
        script.Enabled <- false

    member private this.Invoke(script: Component, callback: Action<Component>, phase: string) =
        let previous = ContactRouter.Current
        ContactRouter.Current <- if phase = "OnBeginSimulation" then script else null

        try
            try
                if not (isNull callback) then
                    callback.Invoke(script)

                true
            with error ->
                this.ReportFailure(script, phase, error)
                false
        finally
            ContactRouter.Current <- previous

    member private this.InvokeTick(script: Component, callback: Action<Component, single>, dt, phase) =
        // Missing members have no callback. No reflection, per-tick closures or
        // native transition per script is introduced by ordinary-member syntax.
        if not (isNull callback) then
            let previous = ContactRouter.Current

            if phase <> "OnTickSimulate" then
                ContactRouter.Current <- null

            try
                try
                    callback.Invoke(script, dt)
                with error ->
                    this.ReportFailure(script, phase, error)
            finally
                ContactRouter.Current <- previous

    member this.ApplyEnabled(script: Component, enabled) =
        if script.SetEnabledState(enabled) then
            if not enabled then
                ContactRouter.Suspend(script)

            if script.InitializeSucceeded then
                let callback =
                    if enabled then
                        script.Lifecycle.OnEnable
                    else
                        script.Lifecycle.OnDisable

                let phase = if enabled then "OnEnable" else "OnDisable"
                this.Invoke(script, callback, phase) |> ignore

    member private this.TearDown(script: Component) =
        if not script.TeardownDelivered then
            script.MarkTeardownDelivered()
            this.ApplyEnabled(script, false)
            script.Lifetime.End()

            if script.BeginSucceeded then
                this.Invoke(script, script.Lifecycle.OnEndSimulation, "OnEndSimulation")
                |> ignore

            if script.EnterSucceeded then
                this.Invoke(script, script.Lifecycle.OnRemovingFromScene, "OnRemovingFromScene")
                |> ignore

            this.Invoke(script, script.Lifecycle.OnUninitializing, "OnUninitializing")
            |> ignore

    member this.FlushRegistrations() =
        if not updating && not flushing then
            flushing <- true

            try
                active.AddRange(pendingAdd)
                pendingAdd.Clear()

                // Cleanup can enqueue more removals. Drain a stable prefix and
                // retain newly enqueued work for the next membership boundary.
                let count = pendingRemove.Count
                let enteredGeneration = generation
                let mutable index = 0

                while index < count && enteredGeneration = generation do
                    let script = pendingRemove[index]
                    active.Remove(script) |> ignore

                    if script.InitializeSucceeded then
                        this.TearDown(script)
                    else
                        script.Lifetime.End()

                    index <- index + 1

                if enteredGeneration = generation then
                    pendingRemove.RemoveRange(0, count)
            finally
                flushing <- false

    member this.DispatchLifecycle(instanceId, phase: int) =
        let script = this.Find(instanceId)
        let phase = enum<Phase>(phase)

        if isNull script || (phase < Phase.OnEndSimulation && not script.IsAlive) then
            false
        elif phase = Phase.OnInitialized then
            if script.IsInitialized then
                false
            else
                script.MarkInitialized()

                if this.Invoke(script, script.Lifecycle.OnInitialized, "OnInitialized") then
                    script.MarkInitializeSucceeded()

                true
        elif not script.InitializeSucceeded then
            false
        else
            match phase with
            | Phase.OnAddedToScene ->
                if this.Invoke(script, script.Lifecycle.OnAddedToScene, "OnAddedToScene") then
                    script.MarkEnterSucceeded()

                    // DDOL reattachment repeats scene entry, never initial enable.
                    if not script.EnterDelivered then
                        script.MarkEnterDelivered()

                        if script.Enabled then
                            this.Invoke(script, script.Lifecycle.OnEnable, "OnEnable") |> ignore

                true
            | Phase.OnBeginSimulation ->
                if
                    script.Enabled
                    && this.Invoke(script, script.Lifecycle.OnBeginSimulation, "OnBeginSimulation")
                then
                    script.MarkBeginSucceeded()

                true
            | Phase.OnEndSimulation ->
                script.MarkTeardownDelivered()
                this.ApplyEnabled(script, false)
                script.Lifetime.End()

                if script.BeginSucceeded then
                    this.Invoke(script, script.Lifecycle.OnEndSimulation, "OnEndSimulation")
                    |> ignore

                true
            | Phase.OnRemovingFromScene ->
                ContactRouter.Suspend(script)

                if script.EnterSucceeded then
                    this.Invoke(script, script.Lifecycle.OnRemovingFromScene, "OnRemovingFromScene")
                    |> ignore

                true
            | Phase.OnUninitializing ->
                this.Invoke(script, script.Lifecycle.OnUninitializing, "OnUninitializing")
                |> ignore

                true
            | _ ->
                Native.Log(2, $"[Lifecycle] Unknown phase: {phase}")
                false

    member this.DispatchEnabled(instanceId, enabled) =
        let script = this.Find(instanceId)

        if isNull script then
            false
        else
            this.ApplyEnabled(script, enabled)
            true

    member this.PrePhysicsTick(dt) =
        this.FlushRegistrations()
        continuations.Drain() |> ignore

        let mutable index = 0

        while index < active.Count do
            let script = active[index]

            if script.IsAlive && script.Enabled then
                this.InvokeTick(script, script.Lifecycle.PrePhysics, dt, "PrePhysics")

            index <- index + 1

    member this.UpdateTick(dt: single) =
        if not Native.IsGameThread then
            invalidOp "OnTickSimulate requires the game thread"

        if updating then
            invalidOp "Reentrant OnTickSimulate dispatch"

        if not (Single.IsFinite(dt)) || dt <= 0.0f then
            invalidArg (nameof dt) "Expected finite positive fixed delta"

        this.FlushRegistrations()
        updating <- true

        try
            let count = active.Count
            let enteredGeneration = generation
            let mutable index = 0

            while index < count && enteredGeneration = generation do
                let script = active[index]

                if
                    script.IsAlive
                    && script.Enabled
                    && script.BeginSucceeded
                    && not script.TeardownDelivered
                then
                    this.InvokeTick(script, script.Lifecycle.OnTickSimulate, dt, "OnTickSimulate")

                index <- index + 1
        finally
            ContactRouter.EndUpdate()
            updating <- false

        this.FlushRegistrations()

    member this.PostPhysicsTick(dt) =
        try
            let mutable index = 0

            while index < active.Count do
                let script = active[index]

                if script.IsAlive && script.Enabled then
                    this.InvokeTick(script, script.Lifecycle.PostPhysics, dt, "PostPhysics")

                index <- index + 1

            this.FlushRegistrations()
        finally
            ContactRouter.EndFrame()

    member this.DispatchMessage(script: Component, message) =
        try
            if not (script.InvokeMessage(message)) then
                Native.Log(2, $"[{script.GetType().Name}] Unknown message: {message}")
        with error ->
            this.ReportFailure(script, message, error)

    member this.SweepOrphans() =
        this.FlushRegistrations()
        let mutable swept = 0

        for index in active.Count - 1 .. -1 .. 0 do
            let script = active[index]

            if not script.IsMarkedDestroyed && not script.Entity.IsAlive then
                this.Remove(script)
                swept <- swept + 1

        swept

    member this.Clear() =
        if not clearing then
            clearing <- true
            generation <- generation + 1u

            try
                let unique = HashSet<Component>(HashIdentity.Reference)
                let retiring = ResizeArray<Component>()

                for components in [ active; pendingAdd; pendingRemove ] do
                    for script in components do
                        if unique.Add(script) then
                            retiring.Add(script)

                // Detach before user cleanup, which can remove siblings or reenter.
                active.Clear()
                pendingAdd.Clear()
                pendingRemove.Clear()
                byOwner.Clear()

                for script in retiring do
                    if script.InitializeSucceeded then
                        this.TearDown(script)
                    else
                        script.Lifetime.End()
            finally
                pendingRemove.Clear()
                clearing <- false

            let dropped = continuations.DropPending()

            if dropped > 0 then
                Native.Log(2, $"[ScriptCore] Reload dropped {dropped} pending continuations.")

            Native.ResetOffThreadReports()
