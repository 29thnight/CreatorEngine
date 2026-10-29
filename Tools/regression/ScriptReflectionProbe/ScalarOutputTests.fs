module ScalarOutputTests

open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine
open CreatorEngine.ProbeNativeTypes

[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionEnableScalarOutputs(int enabled)

let run (check: bool -> string -> unit) (rejected: (unit -> unit) -> string -> string) (entity: Entity) =
    let view = reflection_probe_ComponentView.From(entity)
    let mutable via = false
    let mutable path = AnimatorDataPathValues.FromName("Generation")
    let before = view.diagnosticCalls
    check (view.GetClipCount(&via, &path) = 1UL && via && path.Name = Some "Granular")
        "generated diagnostic outputs have initialized native storage and both scalar writebacks"
    via <- true
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetClipCount(&via, &path) = 100UL && via && path.Name = Some "Granular")
        "each synchronous output call receives its own caller-supplied initial values"

    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetClipName(2, &via, &path) = "clip2" && via && path.Name = Some "Granular")
        "generated string result and diagnostic output parameters"
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetClipFrameCount(3, &via, &path) = 13UL && via && path.Name = Some "Granular")
        "generated clip frame count diagnostic outputs"
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetSkeletonSerial(&via, &path) = 0xf123456789abcdefUL && via && path.Name = Some "Granular")
        "generated diagnostic outputs preserve a full-width native serial result"
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetClipDuration(4, &via, &path) = 4.5 && via && path.Name = Some "Granular")
        "generated double result and diagnostic outputs"
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetBoneCount(&via, &path) = 4UL && via && path.Name = Some "Granular")
        "generated bone count diagnostic outputs"
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.GetBoneName(5, &via, &path) = "bone5" && via && path.Name = Some "Granular")
        "generated bone name diagnostic outputs"
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    check (view.ResolveBoneIndex("hip", &via, &path) = 2 && via && path.Name = Some "Granular")
        "generated const string input and diagnostic outputs"

    let mutable mask = reflection_probe_SettingsView.Create(20, 0.5f, "mask")
    via <- false
    path <- AnimatorDataPathValues.FromName("None")
    let callsBeforeMask = view.calls
    let built = view.BuildAvatarBoneMasks(&mask, &via, &path)
    check (mask.count = 21 && built.Value.count = 21 && not built.IsLive
           && via && path.Name = Some "Granular" && view.calls = callsBeforeMask + 1)
        "mutable JSON record, host scalar outputs and pointer-result snapshot coexist in one call"
    check (built.Value.count = 21 && built.Value.label = "mask" && view.calls = callsBeforeMask + 1)
        "reading a diagnostic method snapshot does not repeat its mutation"
    check (view.diagnosticCalls = before + 10) "all nine whitelisted methods execute once per request"

    via <- true
    path <- AnimatorDataPathValues.FromName("Generation")
    check (view.GetBoneName(-1, &via, &path) = "" && via && path.Name = Some "Generation")
        "a native early return preserves initialized output values when it does not write them"

    let readonlyView = view.GetReadOnlySelf()
    via <- false
    path <- AnimatorDataPathValues.FromName("Generation")
    check (readonlyView.GetClipCount(&via, &path) = 1UL && via && path.Name = Some "Granular")
        "a const query through a readonly alias may write its separate host-owned outputs"
    let beforeReadonly = view.diagnosticCalls
    check (rejected (fun () -> readonlyView.BuildAvatarBoneMasks(&mask, &via, &path) |> ignore)
            "a readonly receiver cannot run a mutable diagnostic method" = "read_only")
        "output storage does not upgrade receiver mutability"
    check (view.diagnosticCalls = beforeReadonly && mask.count = 21)
        "readonly diagnostic rejection occurs before native effects or record writeback"

    let countSpec = NativeMethodSpec("ProbeComponent", "GetClipCount", true,
        [|"bool *"; "AnimatorDataPath *"|], [|"value"; "value"|])
    let output value = NativeCallArgument.FromOutput(JsonSerializer.SerializeToElement(value))
    let beforeInvalid = view.diagnosticCalls
    rejected (fun () -> view.InvokeBound(countSpec, [|output "not a bool"; output "None"|]) |> ignore)
        "invalid bool output initializer rejected" |> ignore
    rejected (fun () -> view.InvokeBound(countSpec, [|output false; output "MissingEnumValue"|]) |> ignore)
        "invalid enum output initializer rejected" |> ignore
    let nameSpec = NativeMethodSpec("ProbeComponent", "GetClipName", true,
        [|"int"; "bool *"; "AnimatorDataPath *"|], [|"value"; "value"; "value"|])
    check (rejected (fun () -> view.InvokeBound(nameSpec, [|output 0; output false; output "None"|]) |> ignore)
            "a non-output parameter index cannot allocate output storage" = "unsupported")
        "the native host approves exact output positions"
    let rawSpec = NativeMethodSpec("ProbeComponent", "RawArgument", false, [|"int *"|], [|"value"|])
    check (rejected (fun () -> view.InvokeBound(rawSpec, [|output 4|]) |> ignore)
            "an unrelated scalar pointer cannot allocate output storage" = "unsupported")
        "the native host approves exact diagnostic method and pointer types"

    let countMethod = countSpec.Resolve()
    let request bindings arguments =
        view.NativeValue.Value.Request("invoke_bound",
            [ "method", box countMethod.Name; "signature", box countMethod.Signature
              "arguments", box arguments; "bindings", box bindings ]) |> ignore
    let boolBinding = ReflectionJson.fields [ "output", box false ]
    let enumBinding = ReflectionJson.fields [ "output", box "None" ]
    let mixedBinding = ReflectionJson.fields [ "output", box false; "callback", box 1UL ]
    check (rejected (fun () -> request [|mixedBinding; enumBinding|] [|null; null|])
            "ambiguous scalar output binding rejected" = "invalid_request")
        "output bindings cannot also select a callback"
    check (rejected (fun () -> request [|boolBinding; enumBinding; enumBinding|] [|null; null|])
            "output binding count rejected" = "invalid_request")
        "an output binding index cannot exceed the selected method arity"
    check (rejected (fun () -> request [|boolBinding; enumBinding|] [|box false; null|])
            "output binding requires null argument placeholder" = "invalid_request")
        "output initializer is accepted only in its explicit binding"
    check (view.diagnosticCalls = beforeInvalid) "all malformed output requests fail before native invocation"

    ProbeReflectionEnableScalarOutputs(0)
    try
        check (rejected (fun () -> view.GetClipCount(&via, &path) |> ignore)
                "missing output host adapter rejected" = "unsupported")
            "scalar outputs require explicit host opt-in"
        check (view.diagnosticCalls = beforeInvalid) "missing output adapter fails before effects"
    finally ProbeReflectionEnableScalarOutputs(1)
    let detached = NativeTypeView.FromSnapshot(JsonSerializer.SerializeToElement(
        System.Collections.Generic.Dictionary<string, obj>()))
    check (rejected (fun () -> detached.InvokeBound(countSpec, [|output false; output "None"|]) |> ignore)
            "detached output receiver rejected" = "read_only")
        "host-owned outputs do not grant a detached snapshot native identity"

    // Scalar output storage is copied back, while an external native argument
    // keeps the established null-output contract (it may be destroyed by a call).
    let buildSpec = NativeMethodSpec("ProbeComponent", "BuildAvatarBoneMasks", false,
        [|"ProbeSettings"; "bool *"; "AnimatorDataPath *"|], [|"mutable_reference"; "value"; "value"|])
    let previousCount = view.settings.count
    let combined = view.InvokeBound(buildSpec, [|view.settings.AsNativeArgument(); output false; output "None"|])
    check (view.settings.count = previousCount + 1 && combined.Arguments.[0].ValueKind = JsonValueKind.Null
           && combined.Arguments.[1].GetBoolean() && combined.Arguments.[2].GetString() = "Granular")
        "ordinary native-bound argument slots remain null beside copied scalar outputs"

    let samples = view.GetHeightSamples()
    check (samples.IsLive && samples.Count = 3 && samples.ToArray() = [|1.5f; -2.0f; 3.25f|])
        "bounded readonly span getter exposes a live path with the backing storage length and values"
    rejected (fun () -> samples.[0] <- 100.0f) "readonly height samples cannot mutate backing native storage" |> ignore
    rejected (fun () -> samples.[3] |> ignore) "height sample index respects the actual span length" |> ignore
    check (samples.[0] = 1.5f && view.GetHeightMap_Unavailable.Reason.Contains("pointer"))
        "readonly samples preserve values while the unbounded raw pointer remains explicitly unavailable"
    let thirdSample = samples.At(2)
    view.ResizeHeightSamples(10000)
    check (samples.Count = 10000 && samples.[9999] = 9.0f && thirdSample.Read(NativeCodecs.Single) = 3.25f)
        "a retained height sample path re-resolves storage after vector reallocation"
    view.ResizeHeightSamples(2)
    check (samples.Count = 2) "a live height span reports the current storage length after shrink"
    rejected (fun () -> thirdSample.Read(NativeCodecs.Single) |> ignore)
        "a removed height sample is rejected after the backing storage shrinks" |> ignore
