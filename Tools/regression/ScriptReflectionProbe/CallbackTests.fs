module CallbackTests

open System
open System.Runtime.CompilerServices
open System.Runtime.InteropServices
open System.Threading.Tasks
open CreatorEngine
open CreatorEngine.ProbeNativeTypes

[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionResetCallbacks()

type private CallbackOwner() =
    inherit Component()

type private CallbackPayload() =
    member val Total = 0.0f with get, set

let private collect () =
    GC.Collect()
    GC.WaitForPendingFinalizers()
    GC.Collect()

[<MethodImpl(MethodImplOptions.NoInlining)>]
let private installRoot (owner: Component) (view: reflection_probe_ComponentView) =
    let payload = CallbackPayload()
    let registration = NativeCallbacks.Float(owner, fun value -> payload.Total <- payload.Total + value)
    view.SetTickCallback(registration)
    registration, WeakReference(payload)

[<MethodImpl(MethodImplOptions.NoInlining)>]
let private rootedTotal (reference: WeakReference) =
    match reference.Target with
    | :? CallbackPayload as payload -> payload.Total
    | _ -> -1.0f

let run (entity: Entity, check: bool -> string -> unit) =
    let view = reflection_probe_ComponentView.From(entity)
    let owner = CallbackOwner()

    let rooted, payload = installRoot owner view
    collect ()
    view.TriggerTick(2.5f)
    check (payload.IsAlive && rootedTotal payload = 2.5f) "registered F# closure survives GC and native owned_func copies"
    rooted.Dispose()
    collect ()
    check (not payload.IsAlive && rooted.IsDisposed) "native disable and retirement release the F# closure root"
    view.TriggerTick(9.0f)
    check rooted.IsDisposed "retained native callback copy stays inert after disposal"

    let mutable voidCalls = 0
    use voidCallback = NativeCallbacks.Void(owner, fun () -> voidCalls <- voidCalls + 1)
    view.RunVoidCallback(voidCallback)
    check (voidCalls = 1) "typed void callback crosses the direct F# native ABI"

    let mutable vector = Float2.Zero
    use vectorCallback = NativeCallbacks.Vector2(owner, fun value -> vector <- value)
    view.RunVectorCallback(vectorCallback)
    check (vector.X = 3.0f && vector.Y = -4.0f) "typed vector2 callback preserves both lanes"

    let mutable text = String.Empty
    let expectedText = "callback 한글 \u0000 tail 🎵"
    use textCallback = NativeCallbacks.Text(owner, fun value -> text <- value)
    view.RunTextCallback(textCallback, expectedText)
    check (text = expectedText) "typed const-reference text callback preserves UTF-8 and embedded NUL"

    let mutable retainedSnapshot: NativeBlackBoardSnapshot option = None
    use condition = NativeCallbacks.Condition(owner, fun board ->
        retainedSnapshot <- Some board
        board.GetValueAsBool("ready"))
    check (view.RunConditionCallback(condition)) "native condition returns the F# boolean result"
    check (retainedSnapshot.Value.GetValueAsBool("ready")) "BlackBoard snapshot survives its native callback scope"
    check (retainedSnapshot.Value.GetValueKind("ready") = NativeBlackBoardValueKind.Bool) "BlackBoard snapshot retains its typed entry kind"

    let callbackError = InvalidOperationException("callback failure stays managed")
    use throwing = NativeCallbacks.Float(owner, fun _ -> raise callbackError)
    view.SetTickCallback(throwing)
    view.TriggerTick(1.0f)
    check (throwing.LastException |> Option.exists (fun error -> obj.ReferenceEquals(error, callbackError)))
        "F# exception is contained and available on the callback registration"
    use falseOnFailure = NativeCallbacks.Condition(owner, fun _ -> raise callbackError)
    check (not (view.RunConditionCallback(falseOnFailure))) "condition exception has a deterministic false result"

    let mutable self: NativeCallback<single> option = None
    let mutable selfCalls = 0
    let selfDisposing = NativeCallbacks.Float(owner, fun _ ->
        selfCalls <- selfCalls + 1
        self.Value.Dispose()
        collect ())
    self <- Some selfDisposing
    view.SetTickCallback(selfDisposing)
    view.TriggerTick(1.0f)
    check (selfCalls = 1 && selfDisposing.IsDisposed) "callback can dispose itself and collect while its native frame remains active"
    NativeCallbackRuntime.Drain()
    view.TriggerTick(1.0f)
    check (selfCalls = 1) "reentrant disposal closes future invocation before releasing the root"

    let mutable reloadWasBlocked = false
    let mutable reloadCalls = 0
    use reloadGuard = NativeCallbacks.Float(owner, fun _ ->
        reloadCalls <- reloadCalls + 1
        try NativeCallbackRuntime.EndGeneration()
        with :? NativeReflectionException as error -> reloadWasBlocked <- error.Code = "callback_busy")
    view.SetTickCallback(reloadGuard)
    view.TriggerTick(1.0f)
    view.TriggerTick(1.0f)
    check (reloadWasBlocked && reloadCalls = 2 && not reloadGuard.IsDisposed)
        "reentrant generation unload is rejected without disabling the live generation"

    let validationRejected =
        use pause = NativeCallbackRuntime.SuspendRegistration()
        try
            use unexpected = NativeCallbacks.Void(owner, ignore)
            false
        with :? NativeReflectionException as error -> error.Code = "callback_retiring"
    check (validationRejected && not reloadGuard.IsDisposed)
        "candidate script validation cannot install callbacks into the retained live generation"

    let workerRejected = Task.Run(fun () ->
        try
            use unexpected = NativeCallbacks.Void(owner, ignore)
            false
        with :? NativeReflectionException -> true).GetAwaiter().GetResult()
    check workerRejected "F# callback registration requires the engine owner thread"

    let scopedOwner = CallbackOwner()
    let mutable scopedCalls = 0
    use scoped = NativeCallbacks.Float(scopedOwner, fun _ -> scopedCalls <- scopedCalls + 1)
    view.SetTickCallback(scoped)
    scopedOwner.Lifetime.End()
    view.TriggerTick(1.0f)
    check (scoped.IsDisposed && scopedCalls = 0) "component lifetime ends its native callback registrations"

    view.SetTickCallback(reloadGuard)
    let oldToken = reloadGuard.Token
    NativeCallbackRuntime.EndGeneration()
    check (reloadGuard.IsDisposed && voidCallback.IsDisposed && textCallback.IsDisposed)
        "generation retirement invalidates all typed callback registrations"
    view.TriggerTick(1.0f)
    check (reloadCalls = 2) "old native callback copies cannot enter a retired F# generation"
    let staleRejected =
        try view.SetTickCallback(reloadGuard); false
        with :? ObjectDisposedException -> true
    check staleRejected "typed invocation refuses a retired callback before sending its token"

    NativeCallbackRuntime.PrepareGeneration()
    let mutable freshCalls = 0
    use fresh = NativeCallbacks.Float(owner, fun _ -> freshCalls <- freshCalls + 1)
    check (fresh.Token <> oldToken) "new script generation receives a distinct callback token"
    view.SetTickCallback(fresh)
    view.TriggerTick(1.0f)
    check (freshCalls = 1) "new generation callback executes after the retirement barrier"

    // Simulate host-owned session invalidation before managed teardown. Every
    // old GC root still requires a native retirement notification; the stale
    // generation can then end and a later binding can register fresh callbacks.
    ProbeReflectionResetCallbacks()
    NativeCallbackRuntime.EndGeneration()
    check fresh.IsDisposed "native session invalidation retires the old managed callback roots"
    view.TriggerTick(1.0f)
    check (freshCalls = 1) "native copies stay inert across a binding session reset"
    NativeCallbackRuntime.PrepareGeneration()
    use rebound = NativeCallbacks.Float(owner, fun _ -> freshCalls <- freshCalls + 1)
    view.SetTickCallback(rebound)
    view.TriggerTick(1.0f)
    check (freshCalls = 2) "callback registration recovers after the previous session retires"
    view.ClearTickCallback()
    owner.Lifetime.End()
    NativeCallbackRuntime.EndGeneration()
    NativeCallbackRuntime.PrepareGeneration()
