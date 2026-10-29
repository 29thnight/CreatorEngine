open System
open System.Reflection
open System.Runtime.InteropServices
open System.Text
open System.Text.Json
open CreatorEngine
open CreatorEngine.NativeTypes
open CreatorEngine.ProbeNativeTypes
open CreatorEngine.Native
open CreatorEngine.Native.reflgen_creator_script_native

[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeReflectionTable()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern uint64 reflgen_creator_script_native_abi_fingerprint()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeReflectionInitialize(nativeint entry, nativeint table, unativeint bytes, uint64 fingerprint)
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionReplace()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionRemove()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionEnablePhysics()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionDestroyOwner()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeReflectionNotifications()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern nativeint ProbeReflectionLastField()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeReflectionCalls()
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern uint64 ProbeReflectionExecute(ScriptObjectHandle owner, nativeint request, int bytes)
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern int ProbeReflectionRead(uint64 ticket, nativeint output, int capacity)
[<DllImport("ScriptReflectionProvider", CallingConvention = CallingConvention.Cdecl)>]
extern void ProbeReflectionRelease(uint64 ticket)

let mutable checks = 0
let check condition message =
    checks <- checks + 1
    if not condition then failwith message

let rejected action message =
    let mutable code = ""
    try action () with :? NativeReflectionException as error -> code <- error.Code
    check (not (String.IsNullOrEmpty(code))) message
    code

let pinned (bytes: byte array) action =
    let handle = GCHandle.Alloc(bytes, GCHandleType.Pinned)
    try action(handle.AddrOfPinnedObject()) finally handle.Free()

let nativeOwner () =
    let mutable owner = Unchecked.defaultof<ScriptObjectHandle>
    owner.index <- 7u
    owner.generation <- 3u
    owner

let rawRequest (bytes: byte array) =
    let ticket = pinned bytes (fun address -> ProbeReflectionExecute(nativeOwner (), address, bytes.Length))
    check (ticket <> 0UL) "raw request reserved a response"
    try
        let size = ProbeReflectionRead(ticket, 0n, 0)
        check (size > 0 && size <= 8 * 1024 * 1024) "raw response obeys the transport limit"
        let response = Array.zeroCreate<byte> size
        check (pinned response (fun address -> ProbeReflectionRead(ticket, address, size)) = size) "raw response read"
        use document = JsonDocument.Parse(response)
        document.RootElement.Clone()
    finally ProbeReflectionRelease(ticket)

let wrapper<'T> (owner: ObjectHandle) (componentId: uint64) =
    Activator.CreateInstance(typeof<'T>, BindingFlags.Instance ||| BindingFlags.Public ||| BindingFlags.NonPublic,
        null, [| box owner; box componentId |], null) :?> 'T

[<EntryPoint>]
let main _ =
    let table = ProbeReflectionTable()
    let fingerprint = reflgen_creator_script_native_abi_fingerprint()
    let initialize = typeof<NativeBootstrap>.GetMethod("Initialize", BindingFlags.Public ||| BindingFlags.Static).MethodHandle.GetFunctionPointer()
    check (ProbeReflectionInitialize(initialize, table, unativeint ScriptApiSize, fingerprint) = 0) "production F# binding initialization"
    let owner = ObjectHandle(7u, 3u)
    let entity = Entity(owner)

    // A complete type closure includes inherited, container-only and cyclic referenced types.
    let catalog = NativeReflection.Types
    let catalogPath = Environment.GetEnvironmentVariable("SCRIPT_REFLECTION_CATALOG_PATH")
    if not (String.IsNullOrEmpty(catalogPath)) then
        System.IO.File.WriteAllBytes(catalogPath, JsonSerializer.SerializeToUtf8Bytes(catalog |> Array.map _.Metadata))
    let byName = catalog |> Array.map (fun item -> item.Name, item) |> Map.ofArray
    check (byName.Count = catalog.Length) "catalog has no duplicate type names"
    check (catalog |> Array.map _.Id |> Array.distinct |> Array.length = catalog.Length) "catalog has no duplicate type IDs"
    for name in [ "ProbeComponent"; "ProbeBase"; "ProbeSettings"; "ProbeCycle" ] do
        check (byName.ContainsKey(name)) ("recursive catalog missing " + name)
    for item in catalog do
        for child in item.Children do check (byName.ContainsKey(child)) ("missing descriptor child " + child)
        for field in item.Fields do check (byName.ContainsKey(field.TypeName)) ("missing descriptor field type " + field.TypeName)
        for method in item.Methods do
            check (byName.ContainsKey(method.ReturnType)) ("missing method return type " + method.ReturnType)
            for parameter in method.Parameters do check (byName.ContainsKey(parameter.TypeName)) ("missing parameter type " + parameter.TypeName)
    check byName.["ProbeComponent"].IsComponent "component metadata"
    check (not byName.["ProbeSettings"].IsComponent) "nested values are not components"
    let reflected = entity.GetNativeComponent("ProbeComponent")
    check (entity.NativeComponents.Length = 1 && reflected.InstanceId = 0xf123456789abcde0UL) "component identity preserves all 64 bits"
    check (entity.TryGetNativeComponent("Absent").IsNone) "unknown component lookup"
    check (reflected.["inherited"].Read<int>() = 11) "inherited field read"
    check (reflected.Invoke("BaseValue", [||]).Read<int>() = 11) "inherited method invocation"
    check (reflected.["title"].Read<string>() = "한글 🎹") "native UTF-16 field reads as UTF-8 JSON"
    let unicode = "바뀐 이름 🌌" + string (char 0) + "embedded"
    reflected.["title"].Write(unicode)
    check (reflected.["title"].Read<string>() = unicode) "UTF-16 write preserves Unicode and embedded NUL"
    reflected.["utf8"].Write("UTF-8 이름 🎹")
    check (reflected.["utf8"].Read<string>() = "UTF-8 이름 🎹") "native UTF-8 field round trip"
    check (reflected.Invoke("Echo", [| box unicode |]).Read<string>() = unicode) "Unicode method argument and result"
    reflected.["engineUtf8"].Write("엔진 UTF-8 🎹")
    check (reflected.["engineUtf8"].Read<string>() = "엔진 UTF-8 🎹") "engine UTF-8 string uses its production adapter"
    check (reflected.Borrow("TitleView").Read<string>() = unicode) "borrowed engine UTF-16 view retains its string contract"
    check (reflected.Invoke("EchoCString", [| box "UTF-8 🎹" |]).Read<string>() = "UTF-8 🎹") "temporary native C string argument storage"
    check (reflected.Invoke("EchoView", [| box unicode |]).Read<string>() = unicode) "temporary native string_view argument storage"
    reflected.["direction"].["x"].Write(1.5f)
    check (reflected.["direction"].["x"].Read<single>() = 1.5f) "production math runtime schema exposes native members"
    check (reflected.["direction"].Describe().Fields.Length = 3) "production vector metadata has every lane"
    let directionSnapshot = reflected.["direction"].ReadJson()
    check (directionSnapshot.ValueKind = JsonValueKind.Object && directionSnapshot.GetProperty("z").GetSingle() = 3.0f) "math whole-value read uses the member schema"
    reflected.["direction"].["y"].Write(9.0f)
    reflected.["direction"].WriteJson(directionSnapshot)
    check (reflected.["direction"].["x"].Read<single>() = 1.5f && reflected.["direction"].["y"].Read<single>() = 2.0f && reflected.["direction"].["z"].Read<single>() = 3.0f) "math ReadJson/WriteJson round trip restores the complete native value"
    let echoedDirection = reflected.Invoke("EchoDirection", [| box directionSnapshot |]).ReturnValue
    check (echoedDirection.ValueKind = JsonValueKind.Object && echoedDirection.GetProperty("x").GetSingle() = 1.5f && echoedDirection.GetProperty("y").GetSingle() = 2.0f && echoedDirection.GetProperty("z").GetSingle() = 3.0f) "math record method arguments and results use the same runtime wire format"
    reflected.["direction"].WriteJson(echoedDirection)
    check (reflected.["direction"].["z"].Read<single>() = 3.0f) "math invocation result can be assigned back to the native value"
    rejected (fun () -> reflected.["lockedDirection"].["x"].Write(0.0f)) "readonly math lanes cannot be written" |> ignore
    rejected (fun () -> reflected.["lockedDirection"].WriteJson(directionSnapshot)) "readonly math whole-value replacement is rejected" |> ignore
    check (reflected.["lockedDirection"].["x"].Read<single>() = 4.0f && reflected.["lockedDirection"].["y"].Read<single>() = 5.0f) "readonly math mutation rejection preserves the native value"
    let aliased = reflected.["aliased"]
    let aliasField = aliased.Describe().Fields |> Array.find (fun field -> field.Name = "count")
    check (aliasField.SerializedName = "wire_count") "field metadata distinguishes native path and serialized names"
    check (aliased.["count"].Read<int>() = 17) "aliased field navigation keeps the native member name"
    let aliasSnapshot = aliased.ReadJson()
    check (aliasSnapshot.GetProperty("wire_count").GetInt32() = 17) "whole record reads honor serialized field names"
    aliased.["count"].Write(29)
    aliased.WriteJson(aliasSnapshot)
    check (aliased.["count"].Read<int>() = 17) "serialized-name whole replacement restores the native field"
    let echoedAlias = reflected.Invoke("EchoAliased", [| box aliasSnapshot |]).ReturnValue
    check (echoedAlias.GetProperty("wire_count").GetInt32() = 17) "serialized names round trip through native method arguments and results"
    aliased.WriteJson(echoedAlias)
    check (aliased.["count"].Read<int>() = 17) "serialized-name method result is writable as a whole value"
    reflected.["engineItems"].[0].["count"].Write(43)
    check (reflected.["engineItems"].[0].["count"].Read<int>() = 43) "engine dynamic_array element navigation"

    let nested = reflected.["settings"].["count"]
    nested.Write(42)
    check (nested.Read<int>() = 42) "nested field write"
    check (Marshal.PtrToStringUTF8(ProbeReflectionLastField()) = "settings") "nested write notifies its root field"
    let notifications = ProbeReflectionNotifications()
    check (nested.Read<int>() = 42 && ProbeReflectionNotifications() = notifications) "read has no mutation notification"
    let second = reflected.["items"].At(1).["count"]
    check (second.Read<int>() = 21) "sequence member read"
    reflected.Invoke("ResizeItems", [| box 10000 |]) |> ignore
    second.Write(55)
    check (second.Read<int>() = 55) "view re-resolves after vector reallocation"
    reflected.Invoke("ResizeItems", [| box 1 |]) |> ignore
    rejected (fun () -> second.Read<int>() |> ignore) "truncated vector invalidates old index" |> ignore
    reflected.["fixed"].[2].Write(33)
    check (reflected.["fixed"].[2].Read<int>() = 33) "fixed array element write"
    rejected (fun () -> reflected.["fixed"].[9].Read<int>() |> ignore) "array bounds checked" |> ignore
    let keyed = reflected.["named"].Key("키").["count"]
    check (keyed.Read<int>() = 31) "Unicode map key lookup"
    keyed.Write(77)
    check (keyed.Read<int>() = 77) "map value nested write"
    reflected.Invoke("EraseNamed", [| box "키" |]) |> ignore
    rejected (fun () -> keyed.Read<int>() |> ignore) "erased map entry invalidates saved path" |> ignore
    reflected.["optional"].Dereference().["count"].Write(88)
    check (reflected.["optional"].Dereference().["count"].Read<int>() = 88) "optional nested write"
    reflected.Invoke("ClearOptional", [||]) |> ignore
    rejected (fun () -> reflected.["optional"].Dereference().["count"].Read<int>() |> ignore) "empty optional dereference" |> ignore
    rejected (fun () -> reflected.["absent"].Dereference().ReadJson() |> ignore) "initially empty optional dereference" |> ignore

    // Read-only policy is transitive through members, containers and borrowed references.
    rejected (fun () -> reflected.["constant"].Write(0)) "const field cannot be written" |> ignore
    rejected (fun () -> reflected.["locked"].["count"].Write(0)) "readonly parent cannot be bypassed through its child" |> ignore
    rejected (fun () -> reflected.["locked"].Invoke("Add", [| box 1 |]) |> ignore) "readonly parent rejects mutable method" |> ignore
    rejected (fun () -> reflected.["locked"].Borrow("MutableSelf").["count"].Write(0)) "const getter cannot upgrade readonly capabilities" |> ignore
    check (reflected.["locked"].Invoke("ReadCount", [||]).Read<int>() = 7) "const method through readonly parent"
    rejected (fun () -> reflected.["readonlySet"].[0].Write(0)) "set keys are read-only" |> ignore
    check (reflected.["bits"].[0].Read<bool>()) "proxy range provides a readable snapshot"
    rejected (fun () -> reflected.["bits"].[0].Write(false)) "vector<bool> snapshot cannot be mutated" |> ignore
    reflected.Borrow("GetSettings").["gain"].Write(0.75f)
    check (reflected.["settings"].["gain"].Read<single>() = 0.75f) "mutable borrowed getter updates the live object"
    rejected (fun () -> reflected.Borrow("GetReadOnlySettings").["gain"].Write(1.0f)) "const borrowed getter is read-only" |> ignore
    let detached = reflected.Borrow("SnapshotSettings")
    check (detached.["count"].Read<int>() = 42) "by-value record getter retains a readable snapshot"
    rejected (fun () -> detached.["count"].Write(0)) "detached record snapshots cannot pretend to update the native owner" |> ignore
    check (reflected.["settings"].["count"].Read<int>() = 42) "detached snapshot rejection preserves the native value"
    let retained = reflected.Borrow("GetShared").Dereference().["count"]
    retained.Write(73)
    check (retained.Read<int>() = 73 && reflected.["shared"].Dereference().["count"].Read<int>() = 73) "by-value shared_owner getter retains and updates its live payload"
    reflected.Invoke("ResetShared", [||]) |> ignore
    check (retained.Read<int>() = 7) "retained owner getter path resolves a replacement payload on each call"
    rejected (fun () -> reflected.["lockedShared"].Dereference().["count"].Write(0)) "readonly owner capability remains readonly through pointer traversal" |> ignore
    rejected (fun () -> reflected.["cycle"].["next"].Dereference().ReadJson() |> ignore) "unapproved raw pointer traversal fails closed" |> ignore
    rejected (fun () -> reflected.Invoke("RawArgument", [| box 0 |]) |> ignore) "unsafe pointer argument is not callable" |> ignore
    let metadata = byName.["ProbeComponent"]
    check (metadata.Fields |> Array.exists (fun field -> field.Name = "constant" && not field.CanWrite)) "const metadata is read-only"
    check (metadata.Methods |> Array.exists (fun method -> method.Name = "GetSettings" && method.CanBorrowResult)) "reference getter capability metadata"
    check (metadata.Methods |> Array.exists (fun method -> method.Name = "RawArgument" && not method.CanInvoke && method.UnavailableReason <> "")) "unsafe method is visible with an explicit reason"

    let before = reflected.["settings"].ReadJson().GetRawText()
    use invalid = JsonDocument.Parse("{\"count\":123,\"gain\":\"bad\",\"label\":\"bad\"}")
    rejected (fun () -> reflected.["settings"].WriteJson(invalid.RootElement)) "invalid nested replacement fails" |> ignore
    check (reflected.["settings"].ReadJson().GetRawText() = before) "failed deserialize does not partially mutate the object"
    let invocation = reflected.Invoke("Adjust", [| box 3 |])
    check (invocation.Arguments.Length = 1 && invocation.Arguments[0].GetInt32() = 45) "mutable reference arguments returned after invocation"
    check (invocation.ReturnValue.ValueKind = JsonValueKind.Null) "void method result"
    rejected (fun () -> reflected.Invoke("Choose", [| box 1 |]) |> ignore) "ambiguous overload requires exact signature" |> ignore
    let integerOverload = metadata.Methods |> Array.find (fun method -> method.Name = "Choose" && method.Parameters[0].TypeName = "int")
    check (reflected.InvokeOverload("Choose", integerOverload.Signature, [| box 1 |]).Read<int>() = 11) "explicit overload selection"
    reflected.["engineNamed"].Key("엔진").["count"].Write(53)
    check (reflected.["engineNamed"].Key("엔진").["count"].Read<int>() = 53) "engine flat_hash_map key navigation"

    // Production response tickets execute a side effect once, even when the caller sizes/retries reads.
    let request = Encoding.UTF8.GetBytes(sprintf "{\"op\":\"invoke\",\"component\":\"ProbeComponent\",\"instance\":%d,\"path\":[],\"method\":\"Increase\",\"arguments\":[2]}" reflected.InstanceId)
    let ticket = pinned request (fun address -> ProbeReflectionExecute(nativeOwner (), address, request.Length))
    check (ticket <> 0UL && ProbeReflectionCalls() = 1) "ticket execute invokes the method once"
    let required = ProbeReflectionRead(ticket, 0n, 0)
    check (required > 0 && ProbeReflectionRead(ticket, 0n, 0) = required && ProbeReflectionCalls() = 1) "repeated size queries do not reexecute"
    let tooSmall = Array.create (required - 1) 0x5auy
    check (pinned tooSmall (fun address -> ProbeReflectionRead(ticket, address, tooSmall.Length)) = required) "short response reports required byte length"
    check (tooSmall |> Array.forall ((=) 0x5auy)) "short response does not partially overwrite caller memory"
    let response = Array.zeroCreate<byte> required
    check (pinned response (fun address -> ProbeReflectionRead(ticket, address, response.Length)) = required) "full response read"
    use result = JsonDocument.Parse(response)
    check (result.RootElement.GetProperty("ok").GetBoolean() && ProbeReflectionCalls() = 1) "retained response remains a single invocation"
    ProbeReflectionRelease(ticket)
    check (ProbeReflectionRead(ticket, 0n, 0) = -1) "released ticket cannot be reused"
    ProbeReflectionRelease(ticket)

    // Malformed requests are rejected before they can mutate an object.
    let rejectRaw text expectedCode message =
        let response = rawRequest (Encoding.UTF8.GetBytes(text: string))
        check (not (response.GetProperty("ok").GetBoolean())) message
        if expectedCode <> "" then check (response.GetProperty("code").GetString() = expectedCode) (message + " code")
    rejectRaw "{" "invalid_value" "malformed JSON is rejected"
    rejectRaw "{\"op\":\"catalog\",\"op\":\"components\"}" "invalid_request" "duplicate JSON properties are rejected"
    rejectRaw (String(' ', 1024 * 1024 + 1)) "limit_exceeded" "native request byte limit"
    rejectRaw ("{\"op\":\"catalog\",\"unused\":" + String('[', 100) + "0" + String(']', 100) + "}") "invalid_value" "deep JSON is rejected"
    let pathRequest path = sprintf "{\"op\":\"get\",\"component\":\"ProbeComponent\",\"instance\":%d,\"path\":%s}" reflected.InstanceId path
    rejectRaw (pathRequest "[{\"field\":\"items\"},{\"index\":-1}]") "invalid_request" "negative container index"
    rejectRaw (pathRequest "[{\"field\":\"settings\",\"index\":0}]") "invalid_request" "ambiguous path segment"
    rejectRaw (pathRequest ("[" + String.concat "," (Array.create 65 "{\"field\":\"settings\"}") + "]")) "limit_exceeded" "member path length limit"
    rejectRaw "{\"op\":\"get\",\"component\":\"ProbeComponent\",\"instance\":18446744073709551616,\"path\":[]}" "invalid_value" "out-of-range component identity"

    // Bound-call options are part of preflight; malformed response projection
    // cannot execute a mutation and then merely hide its successful result.
    let boundCreate extras =
        sprintf "{\"op\":\"invoke_bound\",\"component\":\"ProbeComponent\",\"instance\":%d,\"path\":[],\"method\":\"CreateSettings\",\"arguments\":[999],\"bindings\":[null],%s}" reflected.InstanceId extras
    let beforeMalformedBound = ProbeReflectionCalls()
    rejectRaw (boundCreate "\"discardResult\":\"invalid\"") "invalid_request" "bound discard flag checked before mutation"
    rejectRaw (boundCreate "\"resultPath\":\"invalid\"") "invalid_request" "bound result path shape checked before mutation"
    rejectRaw (boundCreate "\"resultPath\":[{\"field\":\"count\",\"index\":0}]") "invalid_request" "ambiguous bound result segment checked before mutation"
    rejectRaw (boundCreate ("\"resultPath\":[" + String.concat "," (Array.create 65 "{\"deref\":true}") + "]")) "limit_exceeded" "bound result path limit checked before mutation"
    check (ProbeReflectionCalls() = beforeMalformedBound) "malformed bound-call options have no native side effects"
    let readNative = byName.["ProbeComponent"].Methods |> Array.find (fun item -> item.Name = "ReadNativeSettings")
    let emptyPointer = reflected.InvokeBoundOverload("ReadNativeSettings", readNative.Signature,
        [|null|], [|Some reflected.["missingSettings"]|])
    check (emptyPointer.Read<int>() = -1) "bound calls preserve an exact typed null native pointer"

    let callsBeforeCapacity = ProbeReflectionCalls()
    let reserved = Array.init 64 (fun _ -> pinned request (fun address -> ProbeReflectionExecute(nativeOwner (), address, request.Length)))
    check (reserved |> Array.forall ((<>) 0UL)) "bounded response table accepts its documented ticket capacity"
    let overflow = pinned request (fun address -> ProbeReflectionExecute(nativeOwner (), address, request.Length))
    check (overflow = 0UL && ProbeReflectionCalls() = callsBeforeCapacity + 64) "exhausted ticket table rejects before side effects"
    check (ProbeReflectionRead(reserved[0], 0n, 1) = -1 && ProbeReflectionRead(reserved[0], 0n, -1) = -1) "invalid response buffer spans are rejected"
    reserved |> Array.iter ProbeReflectionRelease
    let afterCapacity = rawRequest (Encoding.UTF8.GetBytes("{\"op\":\"components\"}"))
    check (afterCapacity.GetProperty("ok").GetBoolean()) "ticket release restores capacity"

    let threadError = System.Threading.Tasks.Task.Run(fun () -> rejected (fun () -> reflected.["inherited"].Read<int>() |> ignore) "worker access rejected").Result
    check (threadError = "wrong_thread") "worker rejection is explicit"
    let staleTypedPath = TypedSupportTests.run check rejected reflected
    let staleTypedGetter = TypedFacadeTests.run check rejected entity
    CallbackTests.run(entity, check)
    let staleOwnedIdentity = OwnedIdentityTests.run check rejected entity
    ProbeReflectionEnablePhysics()
    let body = wrapper<PhysicsBodyComponent> owner reflected.InstanceId
    let movement = wrapper<CharacterMovementComponent> owner reflected.InstanceId
    let factories = typeof<ScriptComponent>.Assembly.GetType("CreatorEngine.ReflectedComponentBindings", true)
    factories.GetMethod("Register", BindingFlags.Static ||| BindingFlags.Public ||| BindingFlags.NonPublic).Invoke(null, [||]) |> ignore
    let script = entity.GetComponent<ScriptComponent>()
    let invalidScript = entity.GetComponent<InvalidScriptComponent>()
    check (not (isNull script) && script.IsPresent && script.ScriptType = "ProbeScript") "script wrapper factory resolves the native instance"
    check (script.HasInstance && script.ManagedInstanceId = 101 && script.InitializationAttempts = 1UL) "script wrapper native method surface"
    script.ScriptType <- "GeneratedScript"
    script.FieldData <- [|"한글"; "data"|]
    script.Enabled <- false
    check (script.ScriptType = "GeneratedScript" && script.FieldData = [|"한글"; "data"|] && not script.Enabled) "script wrapper native field and enabled mutation"
    check (not (isNull invalidScript) && invalidScript.IsPresent && invalidScript.ErrorMessage = "missing probe script") "invalid script wrapper factory resolves native placeholder"
    check (script.Native.m_scriptType = "GeneratedScript" && invalidScript.Native.m_errorMessage = "missing probe script") "new wrappers expose the generated named Native surface"
    check (body.Reflected.InstanceId = body.ComponentId && body.Reflected.["value"].Read<int>() = 61) "physics wrapper reflection preserves its captured native identity"
    check (movement.Reflected.InstanceId = movement.ComponentId && movement.Reflected.["value"].Read<int>() = 71) "character wrapper reflection preserves its captured native identity"
    let oldNested = reflected.["settings"].["count"]
    let oldNativeArgument = CreatorEngine.ProbeNativeTypes.reflection_probe_ComponentView.From(entity).shared
    ProbeReflectionReplace()
    check (rejected staleOwnedIdentity "owned child rejects component replacement" = "stale_component")
        "owned object identity remains anchored to its original component instance"
    rejected staleTypedPath "typed field view rejects removed or replaced component identity" |> ignore
    rejected staleTypedGetter "generated typed getter rejects removed or replaced component identity" |> ignore
    rejected (fun () -> oldNested.Read<int>() |> ignore) "old component view is rejected after remove/re-add" |> ignore
    rejected (fun () -> body.Reflected |> ignore) "stale physics wrapper cannot retarget a replacement component" |> ignore
    rejected (fun () -> movement.Reflected |> ignore) "stale character wrapper cannot retarget a replacement component" |> ignore
    check (not script.IsPresent && not invalidScript.IsPresent) "new reflected wrapper presence honors captured instance identity"
    rejected (fun () -> script.Native.m_scriptType |> ignore) "script typed wrapper rejects replacement instance" |> ignore
    rejected (fun () -> invalidScript.Native.m_errorMessage |> ignore) "invalid script typed wrapper rejects replacement instance" |> ignore
    let replacement = entity.GetNativeComponent("ProbeComponent")
    check (replacement.InstanceId <> reflected.InstanceId && replacement.["settings"].["count"].Read<int>() = 7) "replacement has a fresh identity"
    let replacementTyped = CreatorEngine.ProbeNativeTypes.reflection_probe_ComponentView.From(entity)
    let beforeStaleBinding = ProbeReflectionCalls()
    rejected (fun () -> replacementTyped.UpdateNativeSettings(oldNativeArgument, 777)) "typed native argument preserves the stale component identity" |> ignore
    check (ProbeReflectionCalls() = beforeStaleBinding && replacementTyped.shared.Value.count = 7) "stale typed pointer argument fails before mutating the replacement"
    let replacementBody = wrapper<PhysicsBodyComponent> owner replacement.InstanceId
    check (replacementBody.Reflected.InstanceId = replacement.InstanceId) "new physics wrapper resolves its replacement identity"
    ProbeReflectionRemove()
    rejected (fun () -> replacement.["settings"].ReadJson() |> ignore) "removed component view is rejected" |> ignore
    check (entity.NativeComponents.Length = 0) "removed component is absent"
    ProbeReflectionDestroyOwner()
    rejected (fun () -> entity.NativeComponents |> ignore) "stale entity owner generation is rejected" |> ignore

    for assembly in [ Assembly.GetExecutingAssembly(); typeof<NativeReflection>.Assembly; typeof<AudioPlaySettings>.Assembly ] do
        for reference in assembly.GetReferencedAssemblies() do
            check (reference.Name <> "ScriptCore" && reference.Name <> "Mono.Cecil" && not (reference.Name.Contains("CSharp"))) ("unexpected runtime bridge " + reference.Name)
    printfn "SCRIPT_REFLECTION_OK checks=%d types=%d api=%d bridge=C#-free" checks catalog.Length ScriptApiVersion
    0
