module OwnedIdentityTests

open System
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine
open CreatorEngine.ProbeNativeTypes

[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionEnableOwnedChildren(int enabled)

type private CallbackOwner() =
    inherit Component()

let private identity (value: NativeTypeView) =
    let path = JsonSerializer.SerializeToElement(value.NativeValue.Value.Path, ReflectionJson.options)
    let owned = path.[0].GetProperty("owned")
    owned.GetProperty("typeId").GetUInt64(), owned.GetProperty("id").GetUInt64()

let private withIdentity (root: NativeValue) typeId childId =
    let owned = ReflectionJson.fields [ "typeId", box typeId; "id", box childId ]
    NativeValue(root.Owner, root.ComponentName, root.NativeInstance,
        [|ReflectionJson.fields [ "owned", box owned ]|])

let run (check: bool -> string -> unit) (rejected: (unit -> unit) -> string -> string) (entity: Entity) =
    let view = reflection_probe_ComponentView.From(entity)
    let beforeCreate = view.calls
    let first = view.AddState("Idle")
    let second = view.AddState("Running")
    check (first.IsLive && second.IsLive && first.HasValue && second.HasValue && view.calls = beforeCreate + 2)
        "generated state creation captures a component-owned identity after exactly one native call"
    check (first.Value.GetName() = "Idle" && first.Value.GetName() = "Idle" && view.calls = beforeCreate + 2)
        "reading a created state does not replay creation"
    let stateType, firstId = identity first
    let _, secondId = identity second
    check (firstId > 0UL && secondId > firstId) "state identities are opaque monotonic IDs"

    let beforeLookup = view.lookupCalls
    let found = view.FindStateByName("Idle")
    check (identity found = identity first && view.lookupCalls = beforeLookup + 1)
        "a state lookup captures the same owned identity in one invocation"
    found.Value.SetName("Renamed")
    let reusedName = view.AddState("Idle")
    check (found.Value.GetName() = "Renamed" && first.Value.GetName() = "Renamed"
           && reusedName.Value.GetName() = "Idle" && view.lookupCalls = beforeLookup + 1)
        "a saved state lookup follows identity through rename and never retargets a reused name"

    let callbackOwner = CallbackOwner()
    let mutable callbackCalls = 0
    use condition = NativeCallbacks.Condition(callbackOwner, fun board ->
        callbackCalls <- callbackCalls + 1
        board.GetValueAsBool("ready"))
    let transition = view.AddTransition(first, second, condition)
    let transitionType, transitionId = identity transition
    check (transition.IsLive && transition.HasValue && transitionType <> stateType && transitionId > secondId)
        "transition creation returns its own typed child identity"
    check (transition.Value.GetFrom().Value.GetName() = "Renamed"
           && transition.Value.GetTo().Value.GetName() = "Running")
        "captured transition endpoints resolve actual native states"
    check (view.EvaluateTransition(transition) && callbackCalls = 1)
        "an actual native transition invokes its rooted F# condition"

    let beforeWrongType = view.calls
    let removeState = NativeMethodSpec("ProbeComponent", "RemoveState", false,
        [|"FSM::FSMState *"|], [|"value"|])
    rejected (fun () -> view.InvokeBound(removeState, [|transition.AsNativeArgument()|], discardResult = true) |> ignore)
        "a transition identity cannot bind a state pointer" |> ignore
    check (view.calls = beforeWrongType && first.Value.GetName() = "Renamed")
        "wrong child pointer type is rejected before native mutation"
    let wrongKind = withIdentity view.NativeValue.Value transitionType firstId
    check (rejected (fun () -> wrongKind.Describe() |> ignore) "a state ID cannot resolve as a transition" = "stale_object")
        "different child kinds share a non-reusable identity space"
    let unsupportedKind = withIdentity view.NativeValue.Value UInt64.MaxValue firstId
    check (rejected (fun () -> unsupportedKind.Describe() |> ignore) "unknown owned type rejected" = "unsupported")
        "the host resolves only explicitly registered child pointer types"

    let malformedPath = NativeValue(view.NativeValue.Value.Owner, "ProbeComponent", view.NativeValue.Value.NativeInstance,
        [|ReflectionJson.fields [ "field", box "settings" ]; wrongKind.Path.[0]|])
    check (rejected (fun () -> malformedPath.Describe() |> ignore) "owned selector cannot replace a nested field" = "invalid_request")
        "owned identities are accepted only as the first component-rooted selector"

    let readonlyView = view.GetReadOnlySelf()
    let beforeReadonlyLookup = view.lookupCalls
    check (rejected (fun () -> readonlyView.FindStateByName("Running") |> ignore) "readonly receiver cannot capture writable identity" = "read_only")
        "readonly result capture has an explicit rejection"
    check (view.lookupCalls = beforeReadonlyLookup) "readonly capture fails before invoking the native lookup"

    let createSettings = NativeMethodSpec("ProbeComponent", "CreateSettings", false, [|"int"|], [|"value"|])
    let beforeUnsupported = view.calls
    check (rejected (fun () -> view.InvokeBoundReference(createSettings,
            [|NativeCallArgument.FromJson(NativeCodecs.Int32.Encode(999))|]) |> ignore)
            "unsupported result cannot become a persistent identity" = "unsupported")
        "owned capture requires a host adapter for the exact result type"
    check (view.calls = beforeUnsupported) "unsupported result capture is rejected before native creation"
    ProbeReflectionEnableOwnedChildren(0)
    try
        check (rejected (fun () -> view.AddState("NotCreated") |> ignore) "missing child adapter fails closed" = "unsupported")
            "a host without identity hooks cannot expose a live pointer result"
        check (view.calls = beforeUnsupported) "missing identity hooks are checked before mutation"
    finally ProbeReflectionEnableOwnedChildren(1)

    let addState = NativeMethodSpec("ProbeComponent", "AddState", false,
        [|"std::string"|], [|"const_reference"|]).Resolve()
    let baseRequest = [ "method", box addState.Name; "signature", box addState.Signature
                        "arguments", box [|box "Malformed"|]; "bindings", box [|(null: obj)|] ]
    let rejectOptions options =
        rejected (fun () -> view.NativeValue.Value.Request("invoke_bound", baseRequest @ options) |> ignore)
            "malformed identity capture options rejected"
    check (rejectOptions [ "captureResult", box "yes" ] = "invalid_request") "capture flag must be a boolean"
    check (rejectOptions [ "captureResult", box true; "discardResult", box true ] = "invalid_request")
        "identity capture and discard are mutually exclusive"
    check (rejectOptions [ "captureResult", box true; "resultPath", box [||] ] = "invalid_request")
        "identity capture and snapshot projection are mutually exclusive"
    check (view.calls = beforeUnsupported) "malformed identity options have no native side effects"

    let beforeRemove = view.calls
    view.RemoveTransition(transition)
    check (view.calls = beforeRemove + 1) "transition removal executes exactly once"
    check (rejected (fun () -> transition.HasValue |> ignore) "removed transition identity is stale" = "stale_object")
        "a captured transition cannot survive logical removal"
    let beforeStale = view.calls
    check (rejected (fun () -> view.RemoveTransition(transition)) "stale transition input rejected" = "stale_object")
        "stale child arguments are checked before the native call"
    check (view.calls = beforeStale) "a rejected stale child cannot trigger a second removal"

    let connected = view.AddTransition(first, second, condition)
    view.RemoveState(first)
    check (rejected (fun () -> first.Value.GetName() |> ignore) "removed state identity is stale" = "stale_object")
        "state removal invalidates its captured views"
    check (rejected (fun () -> found.Value.GetName() |> ignore) "old lookup identity is stale" = "stale_object")
        "captured name lookups do not replay after deletion"
    check (rejected (fun () -> connected.HasValue |> ignore) "connected transition removed with state" = "stale_object")
        "state removal also invalidates connected transition identities"
    check (second.Value.GetName() = "Running" && reusedName.Value.GetName() = "Idle")
        "unrelated live children remain accessible after removal"
    let replacement = view.AddState("Renamed")
    check (snd (identity replacement) > transitionId && identity replacement <> identity first)
        "recreating a state never reuses its removed identity"

    let missing = view.FindStateByName("NeverCreated")
    check (missing.IsLive && not missing.HasValue && snd (identity missing) = 0UL)
        "an absent lookup captures a typed null without manufacturing an address"
    view.RemoveState(missing)
    let beforeNullEndpoint = view.calls
    rejected (fun () -> view.AddTransition(missing, second, condition) |> ignore)
        "a transition cannot use a null endpoint" |> ignore
    check (view.calls = beforeNullEndpoint) "null endpoint validation leaves the native state unchanged"

    let reentrantState = view.AddState("Reentrant")
    use reentrantCondition = NativeCallbacks.Condition(callbackOwner, fun _ ->
        view.RemoveState(reentrantState)
        GC.Collect()
        true)
    let reentrantTransition = view.AddTransition(reentrantState, second, reentrantCondition)
    check (view.EvaluateTransition(reentrantTransition))
        "a pinned transition may remove itself through its F# condition and still return safely"
    check (rejected (fun () -> reentrantTransition.HasValue |> ignore) "reentrantly removed transition is stale" = "stale_object")
        "an in-flight pin does not restore logical membership after callback removal"
    check (rejected (fun () -> reentrantState.Value.GetName() |> ignore) "reentrantly removed state is stale" = "stale_object")
        "callback removal invalidates both state and transition identities"

    view.RemoveState(second)
    view.RemoveState(reusedName)
    view.RemoveState(replacement)
    callbackOwner.Lifetime.End()
    let retained = view.AddState("RetainedUntilComponentReplacement")
    fun () -> retained.Value.GetName() |> ignore
