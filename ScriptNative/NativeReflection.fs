namespace CreatorEngine

open System
open System.Collections.Generic
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine.Native

/// A rejected native reflection operation. Code is stable; Message explains the failing member or path.
type NativeReflectionException(code: string, message: string) =
    inherit InvalidOperationException(message)
    member _.Code = code

module internal ReflectionJson =
    let options = JsonSerializerOptions(IncludeFields = true, MaxDepth = 128)
    let property (name: string) (value: JsonElement) =
        let mutable result = Unchecked.defaultof<JsonElement>
        if value.ValueKind = JsonValueKind.Object && value.TryGetProperty(name, &result) then
            Some result
        else None
    let text name value =
        match property name value with
        | Some item when item.ValueKind = JsonValueKind.String -> item.GetString()
        | _ -> String.Empty
    let flag name value =
        match property name value with
        | Some item when item.ValueKind = JsonValueKind.True -> true
        | _ -> false
    let array name value =
        match property name value with
        | Some item when item.ValueKind = JsonValueKind.Array -> item.EnumerateArray() |> Seq.map _.Clone() |> Seq.toArray
        | _ -> Array.empty
    let uint64 name value =
        match property name value with
        | Some item when item.ValueKind = JsonValueKind.Number -> item.GetUInt64()
        | Some item when item.ValueKind = JsonValueKind.String -> UInt64.Parse(item.GetString(), Globalization.CultureInfo.InvariantCulture)
        | _ -> 0UL
    let fields (values: (string * obj) seq) =
        let result = Dictionary<string, obj>(StringComparer.Ordinal)
        for name, value in values do result.Add(name, value)
        result
    let element<'T> (value: 'T) = JsonSerializer.SerializeToElement(value, options)

module internal NativeReflectionTransport =
    [<Literal>]
    let MaximumRequestBytes = 1024 * 1024
    [<Literal>]
    let MaximumResponseBytes = 8 * 1024 * 1024

    let execute (owner: ObjectHandle) (request: Dictionary<string, obj>) =
        if not NativeBootstrap.IsGameThread then
            raise (NativeReflectionException("wrong_thread", "Native reflection requires the engine owner thread."))
        let api =
            match NativeBootstrap.TryGet() with
            | Some value when not (isNull value.NativeReflection_Execute) &&
                              not (isNull value.NativeReflection_Read) &&
                              not (isNull value.NativeReflection_Release) -> value
            | _ -> raise (NativeReflectionException("unavailable", "The native reflection provider is not initialized."))
        let bytes = JsonSerializer.SerializeToUtf8Bytes(request, ReflectionJson.options)
        if bytes.Length > MaximumRequestBytes then
            invalidArg "request" "The native reflection request exceeds the message limit."
        let requestPin = GCHandle.Alloc(bytes, GCHandleType.Pinned)
        let ticket =
            try api.NativeReflection_Execute.Invoke(owner.ToNative(), requestPin.AddrOfPinnedObject(), bytes.Length)
            finally requestPin.Free()
        if ticket = 0UL then
            raise (NativeReflectionException("transport", "The native reflection provider could not reserve a response."))
        // Execute is called exactly once. Size queries only read the retained response, including for mutating calls.
        try
            let size = api.NativeReflection_Read.Invoke(ticket, 0n, 0)
            if size <= 0 || size > MaximumResponseBytes then
                raise (NativeReflectionException("transport", "Invalid native reflection response size."))
            let response = Array.zeroCreate<byte> size
            let responsePin = GCHandle.Alloc(response, GCHandleType.Pinned)
            let copied =
                try api.NativeReflection_Read.Invoke(ticket, responsePin.AddrOfPinnedObject(), response.Length)
                finally responsePin.Free()
            if copied <> size then
                raise (NativeReflectionException("transport", "The native reflection response changed while being read."))
            use document = JsonDocument.Parse(ReadOnlyMemory<byte>(response), JsonDocumentOptions(MaxDepth = 128))
            let root = document.RootElement
            if not (ReflectionJson.flag "ok" root) then
                raise (NativeReflectionException(ReflectionJson.text "code" root, ReflectionJson.text "message" root))
            match ReflectionJson.property "value" root with
            | Some value -> value.Clone()
            | None -> raise (NativeReflectionException("transport", "The reflection response has no value."))
        finally
            api.NativeReflection_Release.Invoke(ticket)

/// Metadata is copied from the native descriptor graph; it never owns an engine object.
[<Sealed>]
type NativeFieldDescriptor internal (metadata: JsonElement) =
    member _.Name = ReflectionJson.text "name" metadata
    member _.SerializedName = ReflectionJson.text "serializedName" metadata
    member _.TypeName = ReflectionJson.text "type" metadata
    member _.DeclaringType = ReflectionJson.text "declaringType" metadata
    member _.CanRead = ReflectionJson.flag "readable" metadata
    member _.CanWrite = ReflectionJson.flag "writable" metadata
    member _.Metadata = metadata.Clone()

[<Sealed>]
type NativeParameterDescriptor internal (metadata: JsonElement) =
    member _.Name = ReflectionJson.text "name" metadata
    member _.TypeName = ReflectionJson.text "type" metadata
    member _.Passing = ReflectionJson.text "passing" metadata
    member _.Metadata = metadata.Clone()

[<Sealed>]
type NativeMethodDescriptor internal (metadata: JsonElement) =
    member _.Name = ReflectionJson.text "name" metadata
    member _.Signature = ReflectionJson.text "signature" metadata
    member _.ReturnType = ReflectionJson.text "returnType" metadata
    member _.DeclaringType = ReflectionJson.text "declaringType" metadata
    member _.IsConst = ReflectionJson.flag "isConst" metadata
    member _.Parameters = ReflectionJson.array "parameters" metadata |> Array.map NativeParameterDescriptor
    member _.CanInvoke = ReflectionJson.flag "callable" metadata
    member _.CanBorrowResult = ReflectionJson.flag "canBorrow" metadata
    member _.UnavailableReason = ReflectionJson.text "reason" metadata
    member _.Metadata = metadata.Clone()

[<Sealed>]
type NativeTypeDescriptor internal (metadata: JsonElement) =
    member _.Name = ReflectionJson.text "name" metadata
    member _.Id = ReflectionJson.uint64 "id" metadata
    member _.Shape = ReflectionJson.text "shape" metadata
    member _.IsComponent = ReflectionJson.flag "component" metadata
    member _.CanRead = ReflectionJson.flag "readable" metadata
    member _.CanWrite = ReflectionJson.flag "writable" metadata
    member _.IsMutable = ReflectionJson.flag "mutable" metadata
    member _.UnavailableReason = ReflectionJson.text "reason" metadata
    member _.Fields = ReflectionJson.array "fields" metadata |> Array.map NativeFieldDescriptor
    member _.Methods = ReflectionJson.array "methods" metadata |> Array.map NativeMethodDescriptor
    member _.Children = ReflectionJson.array "children" metadata |> Array.map _.GetString()
    member _.Count =
        match ReflectionJson.property "count" metadata with
        | Some value when value.ValueKind = JsonValueKind.Number -> Some (value.GetInt32())
        | _ -> None
    member _.Metadata = metadata.Clone()

/// Return value and ref/out arguments from one native method invocation.
[<Sealed>]
type NativeInvocationResult internal (response: JsonElement) =
    member _.ReturnValue = response.GetProperty("result").Clone()
    member _.Arguments = ReflectionJson.array "arguments" response
    member this.Read<'T>() = this.ReturnValue.Deserialize<'T>(ReflectionJson.options)

/// A live path rooted in a specific native component instance.
/// Every access resolves the entity generation, component instance, and the complete path again.
/// No native address is retained across calls or container reallocations.
type NativeValue internal (owner: ObjectHandle, component: string, instance: uint64,
                           path: Dictionary<string, obj> array) =
    member internal _.Owner = owner
    member internal _.ComponentName = component
    member internal _.NativeInstance = instance
    member internal _.Path = path
    member internal _.Request(operation: string, extras: (string * obj) list) =
        let request = ReflectionJson.fields [ "op", box operation; "component", box component;
                                             "instance", box instance; "path", box path ]
        for name, value in extras do request.Add(name, value)
        NativeReflectionTransport.execute owner request
    member private _.Append(name: string, value: obj) =
        NativeValue(owner, component, instance, Array.append path [| ReflectionJson.fields [ name, value ] |])
    member this.Field(name: string) =
        ArgumentException.ThrowIfNullOrEmpty(name)
        this.Append("field", box name)
    member this.At(index: int) =
        if index < 0 then invalidArg "index" "A native collection index must be non-negative."
        this.Append("index", box index)
    member this.Key(key: string) =
        ArgumentNullException.ThrowIfNull(key)
        this.Append("key", box key)
    member this.Dereference() = this.Append("deref", box true)
    /// Resolve a no-argument getter again on each access. Reference and retained
    /// owner results expose their live target; detached value results are read-only.
    member this.Borrow(methodName: string) =
        ArgumentException.ThrowIfNullOrEmpty(methodName)
        this.Append("method", box methodName)
    member _.BorrowOverload(methodName: string, signature: string) =
        ArgumentException.ThrowIfNullOrEmpty(methodName)
        ArgumentException.ThrowIfNullOrEmpty(signature)
        let segment = ReflectionJson.fields [ "method", box methodName; "signature", box signature ]
        NativeValue(owner, component, instance, Array.append path [| segment |])
    member this.Item
        with get (name: string) = this.Field(name)
    member this.Item
        with get (index: int) = this.At(index)
    member this.Describe() = NativeTypeDescriptor(this.Request("describe", []))
    member this.ReadJson() = this.Request("get", [])
    member this.Read<'T>() = this.ReadJson().Deserialize<'T>(ReflectionJson.options)
    member this.WriteJson(value: JsonElement) = this.Request("set", [ "value", box (value.Clone()) ]) |> ignore
    member this.Write<'T>(value: 'T) = this.WriteJson(ReflectionJson.element value)
    member this.Invoke(methodName: string, [<ParamArray>] arguments: obj array) =
        ArgumentException.ThrowIfNullOrEmpty(methodName)
        ArgumentNullException.ThrowIfNull(arguments)
        NativeInvocationResult(this.Request("invoke", [ "method", box methodName; "arguments", box arguments ]))
    member this.InvokeOverload(methodName: string, signature: string, [<ParamArray>] arguments: obj array) =
        ArgumentException.ThrowIfNullOrEmpty(methodName)
        ArgumentException.ThrowIfNullOrEmpty(signature)
        ArgumentNullException.ThrowIfNull(arguments)
        NativeInvocationResult(this.Request("invoke", [ "method", box methodName; "signature", box signature;
                                                        "arguments", box arguments ]))

/// A generation-checked view of a concrete component, including types with no handwritten F# wrapper.
[<Sealed>]
type NativeObject internal (owner: ObjectHandle, typeName: string, typeId: uint64, instance: uint64) =
    inherit NativeValue(owner, typeName, instance, Array.empty)
    member _.TypeName = typeName
    member _.TypeId = typeId
    member _.InstanceId = instance
    member _.OwnerHandle = owner

/// Complete native component catalog and the recursively reachable reflected value graph.
[<AbstractClass; Sealed>]
type NativeReflection private () =
    static member Types =
        let response = NativeReflectionTransport.execute ObjectHandle.Invalid (ReflectionJson.fields [ "op", box "catalog" ])
        response.EnumerateArray() |> Seq.map (fun item -> NativeTypeDescriptor(item.Clone())) |> Seq.toArray
    static member FindType(name: string) =
        ArgumentException.ThrowIfNullOrEmpty(name)
        NativeReflection.Types |> Array.tryFind (fun item -> item.Name = name)
    static member GetComponents(owner: ObjectHandle) =
        let response = NativeReflectionTransport.execute owner (ReflectionJson.fields [ "op", box "components" ])
        response.EnumerateArray()
        |> Seq.map (fun item -> NativeObject(owner, ReflectionJson.text "type" item,
                                            ReflectionJson.uint64 "typeId" item, ReflectionJson.uint64 "instance" item))
        |> Seq.toArray
    static member TryGetComponent(owner: ObjectHandle, typeName: string) =
        ArgumentException.ThrowIfNullOrEmpty(typeName)
        NativeReflection.GetComponents(owner) |> Array.tryFind (fun item -> item.TypeName = typeName)
    static member GetComponent(owner: ObjectHandle, typeName: string) =
        match NativeReflection.TryGetComponent(owner, typeName) with
        | Some value -> value
        | None -> raise (NativeReflectionException("component_not_found", $"The entity has no native component '{typeName}'."))
