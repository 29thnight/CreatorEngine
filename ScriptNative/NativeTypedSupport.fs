namespace CreatorEngine

open System
open System.Collections
open System.Collections.Generic
open System.Globalization
open System.Text.Json
open System.Text.RegularExpressions
open CreatorEngine.Native

module internal NativeTypedJson =
    let fail code message = raise (NativeReflectionException(code, message))
    let element value = JsonSerializer.SerializeToElement(value, ReflectionJson.options)
    let nullValue = element (null: obj)
    let key (value: JsonElement) =
        if value.ValueKind = JsonValueKind.String then value.GetString() else value.GetRawText()
    let parse text =
        use document = JsonDocument.Parse(text: string)
        document.RootElement.Clone()

    // libclang declarations omit default template arguments, while compiler type names
    // may spell them out. Remove only known defaults, retaining overload identity.
    let private canonicalBaseCv (text: string) =
        // MSVC spells pointee/element qualifiers after their base type. Move
        // only base qualifiers: int const* == const int*, while int* const
        // remains a const pointer and must not become a pointer to const int.
        let mutable depth = 0
        let mutable finish = text.Length
        for index = 0 to text.Length - 1 do
            match text.[index] with
            | '<' -> depth <- depth + 1
            | '>' -> depth <- depth - 1
            | '*' | '&' when depth = 0 && finish = text.Length -> finish <- index
            | _ -> ()
        let baseType = text.Substring(0, finish).TrimEnd()
        let trailing = Regex.Match(baseType, @"\s+((?:const|volatile)(?:\s+(?:const|volatile))*)$")
        if trailing.Success then
            trailing.Groups.[1].Value + " " + baseType.Substring(0, trailing.Index) + text.Substring(finish)
        else text

    let private extentKey (text: string) =
        // Extents are size_t non-type arguments. MSVC prints them in hex (the
        // native type-name implementation deliberately preserves that spelling).
        // Canonicalize this known numeric position only, preserving its value.
        let text = text.Replace("'", "")
        let literal = Regex.Match(text, @"^(0[xX][0-9a-fA-F]+|[0-9]+)(?:[uU](?:[lL]{1,2}|[iI]64)?|[lL]{1,2}[uU]?)?$")
        if not literal.Success then text
        else
            let digits = literal.Groups.[1].Value
            let mutable value = 0UL
            let parsed =
                if digits.StartsWith("0x", StringComparison.OrdinalIgnoreCase) then
                    UInt64.TryParse(digits.Substring(2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, &value)
                else UInt64.TryParse(digits, NumberStyles.None, CultureInfo.InvariantCulture, &value)
            if parsed then value.ToString(CultureInfo.InvariantCulture) else text

    let rec private typeKeyInner stripTopCv (text: string) =
        let text = Regex.Replace(text, @"\b(class|struct|enum)\s+", "")
        let text = text.Replace("std::__cxx11::", "std::").Replace("std::__1::", "std::").Trim()
        let text = if stripTopCv then Regex.Replace(text, @"\s*&&?\s*$", "").Trim() else text
        let text =
            if stripTopCv then
                let text = Regex.Replace(text, @"\s+(const|volatile)$", "")
                if text.EndsWith("*", StringComparison.Ordinal) then text
                else Regex.Replace(text, @"^(const|volatile)\s+", "")
            else text
        let text = canonicalBaseCv text
        let first = text.IndexOf('<')
        let mutable closing = -1
        if first >= 0 then
            let mutable depth = 0
            for index = first to text.Length - 1 do
                if closing < 0 then
                    match text.[index] with
                    | '<' -> depth <- depth + 1
                    | '>' ->
                        depth <- depth - 1
                        if depth = 0 then closing <- index
                    | _ -> ()
        if closing >= 0 then
            // Nested callback parameters retain their qualifiers and indirection,
            // but still need default template arguments and ABI namespaces removed.
            // For example, const basic_string<char, traits, allocator>& and
            // basic_string<char> const& describe the same callback parameter.
            let head = text.Substring(0, first).Trim()
            let qualifiers = Regex.Match(head, @"^(?:(?:const|volatile)\s+)+")
            let prefix = Regex.Replace(qualifiers.Value, @"\s+", "")
            let name = Regex.Replace(head.Substring(qualifiers.Length), @"\s+", "")
            let suffix = Regex.Replace(text.Substring(closing + 1), @"\s+", "")
            let inner = text.Substring(first + 1, closing - first - 1)
            let parts = ResizeArray<string>()
            let mutable depth = 0
            let mutable start = 0
            for index = 0 to inner.Length - 1 do
                match inner.[index] with
                | '<' -> depth <- depth + 1
                | '>' -> depth <- depth - 1
                | ',' when depth = 0 -> parts.Add(typeKeyInner false (inner.Substring(start, index - start))); start <- index + 1
                | _ -> ()
            parts.Add(typeKeyInner false (inner.Substring(start)))
            if (name = "std::array" || name = "std::span" || name = "ce::fixed_array") && parts.Count >= 2 then
                parts.[1] <- extentKey parts.[1]
                // MSVC's type-name spelling uses -1 for span's unsigned
                // dynamic_extent sentinel. This equivalence is span-specific.
                if name = "std::span" && parts.[1] = "-1" then parts.[1] <- "std::dynamic_extent"
            if name = "ce::func_detail::owning_func" && parts.Count >= 6 then
                // Inline capacity/alignment are size_t arguments. Their numeric
                // identity is independent of MSVC's hexadecimal type spelling.
                parts.[4] <- extentKey parts.[4]
                parts.[5] <- extentKey parts.[5]
            let defaults (item: string) =
                item.StartsWith("std::allocator<", StringComparison.Ordinal) ||
                item.StartsWith("std::char_traits<", StringComparison.Ordinal) ||
                item.StartsWith("std::less<", StringComparison.Ordinal) ||
                item.StartsWith("std::hash<", StringComparison.Ordinal) ||
                item.StartsWith("std::equal_to<", StringComparison.Ordinal) ||
                item = "ce::heap_allocator" || item = "std::dynamic_extent" ||
                item = "18446744073709551615" || item = "18446744073709551615ULL"
            let required =
                match name with
                | "std::basic_string" | "std::basic_string_view" | "std::vector" | "std::deque"
                | "std::list" | "std::set" | "std::unordered_set" | "std::span"
                | "ce::dynamic_array" -> 1
                | "std::map" | "std::unordered_map" | "ce::engine_node_map" | "ce::node_hash_map" -> 2
                | _ -> parts.Count
            if name = "ce::dynamic_array" && parts.Count = 3 && parts.[2] = "false" && parts.[1] = "ce::heap_allocator" then
                parts.RemoveAt(2)
            while parts.Count > required && defaults parts.[parts.Count - 1] do parts.RemoveAt(parts.Count - 1)
            let normalized =
                match name, Seq.toArray parts with
                | "std::basic_string", [| "char" |] -> "std::string"
                | "std::basic_string_view", [| "char" |] -> "std::string_view"
                | _ -> name + "<" + String.Join(",", parts) + ">"
            prefix + normalized + suffix
        else
            let text = Regex.Replace(text, @"\s+", " ")
            let text =
                match text with
                | "signed" | "signed int" -> "int"
                | "unsigned" -> "unsigned int"
                | "short int" | "signed short" | "signed short int" -> "short"
                | "unsigned short int" -> "unsigned short"
                | "long int" | "signed long" | "signed long int" -> "long"
                | "unsigned long int" -> "unsigned long"
                | "long long int" | "signed long long" | "signed long long int" | "__int64" -> "long long"
                | "unsigned long long int" | "unsigned __int64" -> "unsigned long long"
                | "std::size_t" | "size_t" ->
                    if UIntPtr.Size = 4 then "unsigned int"
                    elif OperatingSystem.IsWindows() then "unsigned long long"
                    else "unsigned long"
                | _ -> text
            Regex.Replace(text, @"\s+", "")

    let typeKey text = typeKeyInner true text

/// A generated method selector. Resolution uses the current native catalog and
/// caches successful matches only, so loading bindings before the engine is safe.
[<Sealed>]
type NativeMethodSpec(declaringType: string, name: string, isConst: bool,
                      parameterTypes: string array, passing: string array) =
    let parameters = Array.copy parameterTypes
    let passing = Array.copy passing
    let mutable resolved: (obj * NativeMethodDescriptor) option = None
    do
        ArgumentException.ThrowIfNullOrEmpty(declaringType)
        ArgumentException.ThrowIfNullOrEmpty(name)
        if parameters.Length <> passing.Length then invalidArg "passing" "Parameter types and passing modes must have equal lengths."
    member _.DeclaringType = declaringType
    member _.Name = name
    member _.IsConst = isConst
    member _.ParameterTypes = Array.copy parameters
    member _.Passing = Array.copy passing
    member internal _.Resolve() =
        if not NativeBootstrap.IsGameThread then
            NativeTypedJson.fail "wrong_thread" "Native reflection requires the engine owner thread."
        let binding =
            match NativeBootstrap.TryGet() with
            | Some value -> box value
            | None -> NativeTypedJson.fail "unavailable" "The native reflection provider is not initialized."
        match resolved with
        | Some (previous, method) when Object.ReferenceEquals(previous, binding) -> method
        | _ ->
            let nativeType =
                match NativeReflection.FindType(declaringType) with
                | Some value -> value
                | None -> NativeTypedJson.fail "type_not_found" $"Native type '{declaringType}' is unavailable."
            let named = nativeType.Methods |> Array.filter (fun method -> method.Name = name)
            let candidates =
                named |> Array.filter (fun method ->
                    method.IsConst = isConst &&
                    NativeTypedJson.typeKey method.DeclaringType = NativeTypedJson.typeKey declaringType &&
                    method.Parameters.Length = parameters.Length &&
                    Array.forall2 (fun (actual: NativeParameterDescriptor) (expected, mode) ->
                        NativeTypedJson.typeKey actual.TypeName = NativeTypedJson.typeKey expected && actual.Passing = mode)
                        method.Parameters (Array.zip parameters passing))
            match candidates with
            | [| method |] -> resolved <- Some (binding, method); method
            | [||] ->
                let expected = Array.map2 (fun kind mode -> kind + " [" + mode + "]") parameters passing |> String.concat ", "
                let actual =
                    named |> Array.map (fun method ->
                        let arguments = method.Parameters |> Array.map (fun parameter -> parameter.TypeName + " [" + parameter.Passing + "]") |> String.concat ", "
                        $"{method.DeclaringType}::{method.Name}({arguments}) const={method.IsConst}; signature={method.Signature}")
                    |> String.concat " | "
                NativeTypedJson.fail "method_not_found" $"No native overload matches '{declaringType}::{name}' ({expected}) const={isConst}. Available: {actual}"
            | _ -> NativeTypedJson.fail "ambiguous_method" $"Multiple native overloads match '{declaringType}::{name}'."

/// An explicit explanation for a member that has no safe typed ABI representation.
[<Sealed>]
type NativeUnavailableMember(kind: string, declaringType: string, name: string,
                             nativeSignature: string, reason: string) =
    member _.Kind = kind
    member _.DeclaringType = declaringType
    member _.Name = name
    member _.NativeSignature = nativeSignature
    member _.Reason = reason

type private NativeTypedBacking =
    | Live of NativeValue
    | Snapshot of JsonElement

/// A serialized value, live path, callback token, or host-owned scalar output.
/// Native bindings are resolved and type-checked without encoding a process address.
[<Sealed>]
type NativeCallArgument private (value: JsonElement, binding: NativeValue option, callback: uint64 option,
                                 output: JsonElement option) =
    member internal _.Value = value.Clone()
    member internal _.Binding = binding
    member internal _.Callback = callback
    member internal _.Output = output |> Option.map _.Clone()
    static member FromJson(value: JsonElement) = NativeCallArgument(value.Clone(), None, None, None)
    static member FromNative(value: NativeValue) =
        ArgumentNullException.ThrowIfNull(value)
        NativeCallArgument(NativeTypedJson.nullValue, Some value, None, None)
    static member FromCallback(token: uint64) =
        if token = 0UL then invalidArg "token" "Callback tokens must be nonzero."
        NativeCallArgument(NativeTypedJson.nullValue, None, Some token, None)
    static member FromOutput(initialValue: JsonElement) =
        NativeCallArgument(NativeTypedJson.nullValue, None, None, Some(initialValue.Clone()))

/// Shared backing for generated native types. Live paths retain component identity;
/// detached invocation results are immutable JSON snapshots.
type NativeTypeView private (backing: NativeTypedBacking, validate: unit -> unit) =
    new (source: NativeTypeView) = NativeTypeView(source.Backing, source.Validate)
    member private _.Backing = backing
    member private _.Validate() = validate ()
    member internal _.NativeValue = match backing with Live value -> Some value | _ -> None
    member _.IsLive = match backing with Live _ -> true | _ -> false
    static member FromNative(value: NativeValue) =
        ArgumentNullException.ThrowIfNull(value)
        NativeTypeView(Live value, ignore)
    static member FromSnapshot(value: JsonElement) = NativeTypeView(Snapshot(value.Clone()), ignore)
    member internal _.WithGuard(guard: unit -> unit) =
        NativeTypeView(backing, fun () -> validate (); guard ())
    member internal _.EnsureExists() =
        validate ()
        match backing with Live value -> value.Describe() |> ignore | Snapshot _ -> ()
    member _.ReadJson() =
        validate ()
        match backing with Live value -> value.ReadJson() | Snapshot value -> value.Clone()
    member _.WriteJson(value: JsonElement) =
        validate ()
        match backing with
        | Live target -> target.WriteJson(value)
        | Snapshot _ -> NativeTypedJson.fail "read_only" "A detached native result is a read-only snapshot."
    member this.Read<'T>(codec: NativeCodec<'T>) : 'T = codec.Decode(this)
    member this.Write<'T>(codec: NativeCodec<'T>, value: 'T) : unit = this.WriteJson(codec.Encode(value))
    member _.Field(nativeName: string, serializedName: string) =
        ArgumentException.ThrowIfNullOrEmpty(nativeName)
        let next =
            match backing with
            | Live value -> Live(value.Field(nativeName))
            | Snapshot value ->
                let mutable field = Unchecked.defaultof<JsonElement>
                if value.ValueKind <> JsonValueKind.Object || not (value.TryGetProperty(serializedName, &field)) then
                    NativeTypedJson.fail "member_not_found" $"Snapshot member '{serializedName}' is unavailable."
                Snapshot(field.Clone())
        NativeTypeView(next, validate)
    member _.At(index: int) =
        if index < 0 then invalidArg "index" "A native collection index must be non-negative."
        let next =
            match backing with
            | Live value -> Live(value.At(index))
            | Snapshot value ->
                if value.ValueKind <> JsonValueKind.Array || index >= value.GetArrayLength() then
                    NativeTypedJson.fail "out_of_range" "The snapshot index is outside its array."
                Snapshot(value.[index].Clone())
        NativeTypeView(next, validate)
    member _.Key(key: string) =
        ArgumentNullException.ThrowIfNull(key)
        let next =
            match backing with
            | Live value -> Live(value.Key(key))
            | Snapshot value ->
                let matches =
                    match value.ValueKind with
                    | JsonValueKind.Object -> value.EnumerateObject() |> Seq.filter (fun item -> item.Name = key) |> Seq.map _.Value |> Seq.toArray
                    | JsonValueKind.Array ->
                        value.EnumerateArray()
                        |> Seq.filter (fun item -> item.ValueKind = JsonValueKind.Array && item.GetArrayLength() = 2 && NativeTypedJson.key item.[0] = key)
                        |> Seq.map (fun item -> item.[1]) |> Seq.toArray
                    | _ -> NativeTypedJson.fail "type_mismatch" "The snapshot is not a native map."
                match matches with
                | [| item |] -> Snapshot(item.Clone())
                | [||] -> NativeTypedJson.fail "key_not_found" $"The snapshot has no map key '{key}'."
                | _ -> NativeTypedJson.fail "ambiguous_key" $"The snapshot has multiple matching keys '{key}'."
        NativeTypeView(next, validate)
    member _.Dereference() =
        let next =
            match backing with
            | Live value -> Live(value.Dereference())
            | Snapshot value when value.ValueKind = JsonValueKind.Null -> NativeTypedJson.fail "null_value" "The native optional or reference is empty."
            | Snapshot value -> Snapshot value
        NativeTypeView(next, validate)
    member internal _.Count =
        validate ()
        match backing with
        | Live value ->
            match value.Describe().Count with
            | Some count -> count
            | None -> NativeTypedJson.fail "unsupported" "This native value does not expose indexed storage."
        | Snapshot value ->
            match value.ValueKind with
            | JsonValueKind.Array -> value.GetArrayLength()
            | JsonValueKind.Object -> value.EnumerateObject() |> Seq.length
            | JsonValueKind.Null -> 0
            | _ -> NativeTypedJson.fail "type_mismatch" "The snapshot is not a collection."
    member internal this.HasTarget =
        match backing with
        | Live _ -> this.Count <> 0
        | Snapshot _ -> this.ReadJson().ValueKind <> JsonValueKind.Null
    member _.Invoke(spec: NativeMethodSpec, arguments: JsonElement array) =
        validate ()
        match backing with
        | Snapshot _ -> NativeTypedJson.fail "read_only" "Detached native snapshots cannot invoke native methods."
        | Live value ->
            let method = spec.Resolve()
            if not method.CanInvoke then NativeTypedJson.fail "unsupported" method.UnavailableReason
            value.InvokeOverload(method.Name, method.Signature, arguments |> Array.map (fun item -> box (item.Clone())))
    member _.AsNativeArgument() =
        validate ()
        match backing with
        | Live value -> NativeCallArgument.FromNative(value)
        | Snapshot _ -> NativeTypedJson.fail "detached_argument" "This parameter requires a live native object or owner; a JSON snapshot has no native identity."
    member this.AsValueArgument() = NativeCallArgument.FromJson(this.ReadJson())
    /// Invoke with native paths and serialized values. Returned native objects
    /// are read once within the request and exposed only as detached snapshots.
    member _.InvokeBound(spec: NativeMethodSpec, arguments: NativeCallArgument array, ?discardResult: bool) =
        validate ()
        match backing with
        | Snapshot _ -> NativeTypedJson.fail "read_only" "Detached native snapshots cannot invoke native methods."
        | Live value ->
            let method = spec.Resolve()
            if not method.CanInvokeBound then NativeTypedJson.fail "unsupported" method.BoundUnavailableReason
            value.InvokeBoundOverload(method.Name, method.Signature,
                arguments |> Array.map (fun argument -> box argument.Value),
                arguments |> Array.map _.Binding, ?discardResult = discardResult,
                callbackTokens = (arguments |> Array.map _.Callback),
                outputValues = (arguments |> Array.map _.Output))
    /// Capture only a host-registered child identity. The engine resolves its
    /// membership again on every access; no returned native address is stored.
    member _.InvokeBoundReference(spec: NativeMethodSpec, arguments: NativeCallArgument array) =
        validate ()
        match backing with
        | Snapshot _ -> NativeTypedJson.fail "read_only" "Detached native snapshots cannot capture owned native objects."
        | Live value ->
            let method = spec.Resolve()
            if not method.CanInvokeBound then NativeTypedJson.fail "unsupported" method.BoundUnavailableReason
            let response = value.InvokeBoundOverload(method.Name, method.Signature,
                arguments |> Array.map (fun argument -> box argument.Value),
                arguments |> Array.map _.Binding,
                callbackTokens = (arguments |> Array.map _.Callback),
                outputValues = (arguments |> Array.map _.Output), captureResult = true)
            response, NativeTypeView.FromNative(value.CapturedResult(response))
    member _.Borrow(spec: NativeMethodSpec, arguments: JsonElement array) =
        validate ()
        match backing with
        | Snapshot _ -> NativeTypedJson.fail "read_only" "Detached native snapshots cannot evaluate native getters."
        | Live value ->
            let method = spec.Resolve()
            if not method.CanBorrowResult || (arguments.Length <> 0 && not method.CanBorrowWithArguments) then
                NativeTypedJson.fail "unsupported" method.BorrowUnavailableReason
            let result = value.BorrowOverload(method.Name, method.Signature, arguments |> Array.map (fun item -> box (item.Clone())))
            NativeTypeView(Live result, validate)
    member this.Borrow(spec: NativeMethodSpec) = this.Borrow(spec, Array.empty)

/// A wire codec keeps native records and containers as typed views instead of
/// serializing managed wrapper implementation details.
and NativeCodec<'T>(decode: NativeTypeView -> 'T, encode: 'T -> JsonElement, ?decodeKey: string -> 'T) =
    member _.Decode(source: NativeTypeView) : 'T = decode source
    member _.Encode(value: 'T) : JsonElement = encode value
    member internal _.DecodeKey(key: string) : 'T =
        match decodeKey with
        | Some read -> read key
        | None -> decode (NativeTypeView.FromSnapshot(NativeTypedJson.parse key))

/// Native enums may use symbolic names or integers, including unknown flag combinations.
[<Sealed>]
type NativeEnumValue<'Tag> private (nativeTypeName: string, value: JsonElement) =
    member _.NativeTypeName = nativeTypeName
    member _.Value = value.Clone()
    member _.Name = if value.ValueKind = JsonValueKind.String then Some(value.GetString()) else None
    member _.TryInt64 =
        let mutable number = 0L
        if value.ValueKind = JsonValueKind.Number && value.TryGetInt64(&number) then Some number else None
    member _.TryUInt64 =
        let mutable number = 0UL
        if value.ValueKind = JsonValueKind.Number && value.TryGetUInt64(&number) then Some number else None
    override _.ToString() = if value.ValueKind = JsonValueKind.String then value.GetString() else value.GetRawText()
    static member FromName(nativeTypeName: string, value: string) = NativeEnumValue<'Tag>(nativeTypeName, NativeTypedJson.element value)
    static member FromInt64(nativeTypeName: string, value: int64) = NativeEnumValue<'Tag>(nativeTypeName, NativeTypedJson.element value)
    static member FromUInt64(nativeTypeName: string, value: uint64) = NativeEnumValue<'Tag>(nativeTypeName, NativeTypedJson.element value)
    static member internal FromJson(nativeTypeName: string, value: JsonElement) =
        if value.ValueKind <> JsonValueKind.String && value.ValueKind <> JsonValueKind.Number then
            NativeTypedJson.fail "type_mismatch" "A native enum requires a name or integer."
        NativeEnumValue<'Tag>(nativeTypeName, value.Clone())

type NativeArray<'T> internal (source: NativeTypeView, codec: NativeCodec<'T>) =
    inherit NativeTypeView(source)
    static member FromArray(values: 'T array, codec: NativeCodec<'T>) =
        ArgumentNullException.ThrowIfNull(values)
        let wire = values |> Array.map codec.Encode |> NativeTypedJson.element
        NativeArray<'T>(NativeTypeView.FromSnapshot(wire), codec)
    static member Empty(codec: NativeCodec<'T>) = NativeArray<'T>.FromArray(Array.empty, codec)
    member _.Count = source.Count
    member _.Item
        with get(index: int) = codec.Decode(source.At(index))
        and set(index: int) (value: 'T) = source.At(index).Write(codec, value)
    member this.ToArray() = Array.init this.Count (fun index -> this.[index])
    interface IReadOnlyList<'T> with
        member this.Count = this.Count
        member this.Item with get(index) = this.[index]
    interface IEnumerable<'T> with
        member this.GetEnumerator() = (seq { for index = 0 to this.Count - 1 do yield this.[index] }).GetEnumerator()
    interface IEnumerable with
        member this.GetEnumerator() = ((this :> IEnumerable<'T>).GetEnumerator() :> IEnumerator)

type NativeMap<'Key, 'Value> internal (source: NativeTypeView, keyCodec: NativeCodec<'Key>, valueCodec: NativeCodec<'Value>) =
    inherit NativeTypeView(source)
    member _.Count = source.Count
    member _.Keys =
        let value = source.ReadJson()
        match value.ValueKind with
        | JsonValueKind.Object -> value.EnumerateObject() |> Seq.map (fun item -> keyCodec.DecodeKey(item.Name)) |> Seq.toArray
        | JsonValueKind.Array -> value.EnumerateArray() |> Seq.map (fun pair -> keyCodec.Decode(NativeTypeView.FromSnapshot(pair.[0]))) |> Seq.toArray
        | _ -> NativeTypedJson.fail "type_mismatch" "The value is not a native map."
    member _.Item
        with get(key: 'Key) = valueCodec.Decode(source.Key(NativeTypedJson.key (keyCodec.Encode(key))))
        and set(key: 'Key) (value: 'Value) = source.Key(NativeTypedJson.key (keyCodec.Encode(key))).Write(valueCodec, value)
    member this.TryGetValue(key: 'Key) =
        try
            let selected = source.Key(NativeTypedJson.key (keyCodec.Encode(key)))
            selected.EnsureExists()
            Some(valueCodec.Decode(selected))
        with :? NativeReflectionException as error when error.Code = "key_not_found" -> None

type NativeReference<'T> internal (source: NativeTypeView, codec: NativeCodec<'T>) =
    inherit NativeTypeView(source)
    member _.HasValue = source.HasTarget
    member _.Value = codec.Decode(source.Dereference())
    member this.TryValue = if this.HasValue then Some this.Value else None

type NativeOptional<'T> internal (source: NativeTypeView, codec: NativeCodec<'T>) =
    inherit NativeReference<'T>(source, codec)
    member _.Set(value: 'T) = source.Write(codec, value)
    member _.Clear() = source.WriteJson(NativeTypedJson.nullValue)

type NativeExpected<'T, 'Error> internal (source: NativeTypeView, valueCodec: NativeCodec<'T>, errorCodec: NativeCodec<'Error>) =
    inherit NativeTypeView(source)
    let branch () =
        let value = source.ReadJson()
        if value.ValueKind <> JsonValueKind.Object then NativeTypedJson.fail "type_mismatch" "An expected value requires a result object."
        let mutable success = Unchecked.defaultof<JsonElement>
        let mutable failure = Unchecked.defaultof<JsonElement>
        let hasValue = value.TryGetProperty("value", &success)
        let hasError = value.TryGetProperty("error", &failure)
        if hasValue = hasError then NativeTypedJson.fail "type_mismatch" "An expected value requires exactly one result branch."
        hasValue, (if hasValue then success else failure)
    let child success =
        let active, value = branch ()
        if active <> success then NativeTypedJson.fail "inactive_alternative" "The expected result branch is inactive."
        if source.IsLive && not (success && typeof<'T> = typeof<unit>) then
            source.At(0).WithGuard(fun () ->
                if fst (branch ()) <> success then NativeTypedJson.fail "inactive_alternative" "The expected result branch changed.")
        else NativeTypeView.FromSnapshot(value)
    member _.IsSuccess = fst (branch ())
    member _.Value = valueCodec.Decode(child true)
    member _.Error = errorCodec.Decode(child false)
    member this.TryValue = if this.IsSuccess then Some this.Value else None
    member this.TryError = if this.IsSuccess then None else Some this.Error
    member _.SetValue(value: 'T) = source.WriteJson(NativeTypedJson.element (dict [ "value", valueCodec.Encode(value) ]))
    member _.SetError(value: 'Error) = source.WriteJson(NativeTypedJson.element (dict [ "error", errorCodec.Encode(value) ]))

type NativeVariant internal (source: NativeTypeView) =
    inherit NativeTypeView(source)
    member _.Index =
        let value = source.ReadJson()
        if value.ValueKind <> JsonValueKind.Array || value.GetArrayLength() <> 2 then
            NativeTypedJson.fail "type_mismatch" "A variant requires an index and payload pair."
        value.[0].GetInt32()
    member internal this.Case(index: int, codec: NativeCodec<'T>) =
        if this.Index <> index then None
        else
            let child =
                if source.IsLive then
                    source.At(0).WithGuard(fun () ->
                        if this.Index <> index then NativeTypedJson.fail "inactive_alternative" "The native variant alternative changed.")
                else source.At(1)
            Some(codec.Decode(child))
    member internal _.SetCase(index: int, codec: NativeCodec<'T>, value: 'T) =
        source.WriteJson(NativeTypedJson.element [| NativeTypedJson.element index; codec.Encode(value) |])

/// A typed native tuple with 2 elements.
type NativeTuple2<'T1, 'T2> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)

/// A typed native tuple with 3 elements.
type NativeTuple3<'T1, 'T2, 'T3> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)

/// A typed native tuple with 4 elements.
type NativeTuple4<'T1, 'T2, 'T3, 'T4> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)
    member _.Item4
        with get() = codec4.Decode(source.At(3))
        and set(value: 'T4) = source.At(3).Write(codec4, value)

/// A typed native tuple with 5 elements.
type NativeTuple5<'T1, 'T2, 'T3, 'T4, 'T5> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)
    member _.Item4
        with get() = codec4.Decode(source.At(3))
        and set(value: 'T4) = source.At(3).Write(codec4, value)
    member _.Item5
        with get() = codec5.Decode(source.At(4))
        and set(value: 'T5) = source.At(4).Write(codec5, value)

/// A typed native tuple with 6 elements.
type NativeTuple6<'T1, 'T2, 'T3, 'T4, 'T5, 'T6> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)
    member _.Item4
        with get() = codec4.Decode(source.At(3))
        and set(value: 'T4) = source.At(3).Write(codec4, value)
    member _.Item5
        with get() = codec5.Decode(source.At(4))
        and set(value: 'T5) = source.At(4).Write(codec5, value)
    member _.Item6
        with get() = codec6.Decode(source.At(5))
        and set(value: 'T6) = source.At(5).Write(codec6, value)

/// A typed native tuple with 7 elements.
type NativeTuple7<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)
    member _.Item4
        with get() = codec4.Decode(source.At(3))
        and set(value: 'T4) = source.At(3).Write(codec4, value)
    member _.Item5
        with get() = codec5.Decode(source.At(4))
        and set(value: 'T5) = source.At(4).Write(codec5, value)
    member _.Item6
        with get() = codec6.Decode(source.At(5))
        and set(value: 'T6) = source.At(5).Write(codec6, value)
    member _.Item7
        with get() = codec7.Decode(source.At(6))
        and set(value: 'T7) = source.At(6).Write(codec7, value)

/// A typed native tuple with 8 elements.
type NativeTuple8<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)
    member _.Item4
        with get() = codec4.Decode(source.At(3))
        and set(value: 'T4) = source.At(3).Write(codec4, value)
    member _.Item5
        with get() = codec5.Decode(source.At(4))
        and set(value: 'T5) = source.At(4).Write(codec5, value)
    member _.Item6
        with get() = codec6.Decode(source.At(5))
        and set(value: 'T6) = source.At(5).Write(codec6, value)
    member _.Item7
        with get() = codec7.Decode(source.At(6))
        and set(value: 'T7) = source.At(6).Write(codec7, value)
    member _.Item8
        with get() = codec8.Decode(source.At(7))
        and set(value: 'T8) = source.At(7).Write(codec8, value)

/// A typed native tuple with 9 elements.
type NativeTuple9<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8, 'T9> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>, codec9: NativeCodec<'T9>) =
    inherit NativeTypeView(source)
    member _.Item1
        with get() = codec1.Decode(source.At(0))
        and set(value: 'T1) = source.At(0).Write(codec1, value)
    member _.Item2
        with get() = codec2.Decode(source.At(1))
        and set(value: 'T2) = source.At(1).Write(codec2, value)
    member _.Item3
        with get() = codec3.Decode(source.At(2))
        and set(value: 'T3) = source.At(2).Write(codec3, value)
    member _.Item4
        with get() = codec4.Decode(source.At(3))
        and set(value: 'T4) = source.At(3).Write(codec4, value)
    member _.Item5
        with get() = codec5.Decode(source.At(4))
        and set(value: 'T5) = source.At(4).Write(codec5, value)
    member _.Item6
        with get() = codec6.Decode(source.At(5))
        and set(value: 'T6) = source.At(5).Write(codec6, value)
    member _.Item7
        with get() = codec7.Decode(source.At(6))
        and set(value: 'T7) = source.At(6).Write(codec7, value)
    member _.Item8
        with get() = codec8.Decode(source.At(7))
        and set(value: 'T8) = source.At(7).Write(codec8, value)
    member _.Item9
        with get() = codec9.Decode(source.At(8))
        and set(value: 'T9) = source.At(8).Write(codec9, value)

/// A typed native variant with 2 elements.
type NativeVariant2<'T1, 'T2> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)

/// A typed native variant with 3 elements.
type NativeVariant3<'T1, 'T2, 'T3> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)

/// A typed native variant with 4 elements.
type NativeVariant4<'T1, 'T2, 'T3, 'T4> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)
    member this.Case4 = this.Case(3, codec4)
    member this.SetCase4(value: 'T4) = this.SetCase(3, codec4, value)

/// A typed native variant with 5 elements.
type NativeVariant5<'T1, 'T2, 'T3, 'T4, 'T5> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)
    member this.Case4 = this.Case(3, codec4)
    member this.SetCase4(value: 'T4) = this.SetCase(3, codec4, value)
    member this.Case5 = this.Case(4, codec5)
    member this.SetCase5(value: 'T5) = this.SetCase(4, codec5, value)

/// A typed native variant with 6 elements.
type NativeVariant6<'T1, 'T2, 'T3, 'T4, 'T5, 'T6> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)
    member this.Case4 = this.Case(3, codec4)
    member this.SetCase4(value: 'T4) = this.SetCase(3, codec4, value)
    member this.Case5 = this.Case(4, codec5)
    member this.SetCase5(value: 'T5) = this.SetCase(4, codec5, value)
    member this.Case6 = this.Case(5, codec6)
    member this.SetCase6(value: 'T6) = this.SetCase(5, codec6, value)

/// A typed native variant with 7 elements.
type NativeVariant7<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)
    member this.Case4 = this.Case(3, codec4)
    member this.SetCase4(value: 'T4) = this.SetCase(3, codec4, value)
    member this.Case5 = this.Case(4, codec5)
    member this.SetCase5(value: 'T5) = this.SetCase(4, codec5, value)
    member this.Case6 = this.Case(5, codec6)
    member this.SetCase6(value: 'T6) = this.SetCase(5, codec6, value)
    member this.Case7 = this.Case(6, codec7)
    member this.SetCase7(value: 'T7) = this.SetCase(6, codec7, value)

/// A typed native variant with 8 elements.
type NativeVariant8<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)
    member this.Case4 = this.Case(3, codec4)
    member this.SetCase4(value: 'T4) = this.SetCase(3, codec4, value)
    member this.Case5 = this.Case(4, codec5)
    member this.SetCase5(value: 'T5) = this.SetCase(4, codec5, value)
    member this.Case6 = this.Case(5, codec6)
    member this.SetCase6(value: 'T6) = this.SetCase(5, codec6, value)
    member this.Case7 = this.Case(6, codec7)
    member this.SetCase7(value: 'T7) = this.SetCase(6, codec7, value)
    member this.Case8 = this.Case(7, codec8)
    member this.SetCase8(value: 'T8) = this.SetCase(7, codec8, value)

/// A typed native variant with 9 elements.
type NativeVariant9<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8, 'T9> internal (source: NativeTypeView, codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>, codec9: NativeCodec<'T9>) =
    inherit NativeVariant(source)
    member this.Case1 = this.Case(0, codec1)
    member this.SetCase1(value: 'T1) = this.SetCase(0, codec1, value)
    member this.Case2 = this.Case(1, codec2)
    member this.SetCase2(value: 'T2) = this.SetCase(1, codec2, value)
    member this.Case3 = this.Case(2, codec3)
    member this.SetCase3(value: 'T3) = this.SetCase(2, codec3, value)
    member this.Case4 = this.Case(3, codec4)
    member this.SetCase4(value: 'T4) = this.SetCase(3, codec4, value)
    member this.Case5 = this.Case(4, codec5)
    member this.SetCase5(value: 'T5) = this.SetCase(4, codec5, value)
    member this.Case6 = this.Case(5, codec6)
    member this.SetCase6(value: 'T6) = this.SetCase(5, codec6, value)
    member this.Case7 = this.Case(6, codec7)
    member this.SetCase7(value: 'T7) = this.SetCase(6, codec7, value)
    member this.Case8 = this.Case(7, codec8)
    member this.SetCase8(value: 'T8) = this.SetCase(7, codec8, value)
    member this.Case9 = this.Case(8, codec9)
    member this.SetCase9(value: 'T9) = this.SetCase(8, codec9, value)

/// Codecs used by generated member declarations. All wire values remain JSON,
/// while records and containers retain their live identity or snapshot policy.
[<AbstractClass; Sealed>]
type NativeCodecs private () =
    static member Bool = NativeCodec<bool>((fun source -> source.ReadJson().GetBoolean()), NativeTypedJson.element)
    static member Int8 = NativeCodec<sbyte>((fun source -> source.ReadJson().GetSByte()), NativeTypedJson.element)
    static member UInt8 = NativeCodec<byte>((fun source -> source.ReadJson().GetByte()), NativeTypedJson.element)
    static member Int16 = NativeCodec<int16>((fun source -> source.ReadJson().GetInt16()), NativeTypedJson.element)
    static member UInt16 = NativeCodec<uint16>((fun source -> source.ReadJson().GetUInt16()), NativeTypedJson.element)
    static member Int32 = NativeCodec<int>((fun source -> source.ReadJson().GetInt32()), NativeTypedJson.element)
    static member UInt32 = NativeCodec<uint32>((fun source -> source.ReadJson().GetUInt32()), NativeTypedJson.element)
    static member Int64 = NativeCodec<int64>((fun source -> source.ReadJson().GetInt64()), NativeTypedJson.element)
    static member UInt64 = NativeCodec<uint64>((fun source -> source.ReadJson().GetUInt64()), NativeTypedJson.element)
    static member Single = NativeCodec<single>((fun source -> source.ReadJson().GetSingle()), NativeTypedJson.element)
    static member Double = NativeCodec<double>((fun source -> source.ReadJson().GetDouble()), NativeTypedJson.element)
    static member String = NativeCodec<string>((fun source -> source.ReadJson().GetString()), NativeTypedJson.element, decodeKey = id)
    static member Guid = NativeCodec<Guid>((fun source -> Guid.Parse(source.ReadJson().GetString())),
        (fun value -> NativeTypedJson.element (value.ToString("D"))), decodeKey = Guid.Parse)
    static member Json = NativeCodec<JsonElement>((fun source -> source.ReadJson()), fun value -> value.Clone())
    static member Unit =
        NativeCodec<unit>((fun source ->
            if source.ReadJson().ValueKind <> JsonValueKind.Null then NativeTypedJson.fail "type_mismatch" "A void result must be null."),
            fun () -> NativeTypedJson.nullValue)
    static member Enum<'Tag>(nativeTypeName: string) =
        let read value = NativeEnumValue<'Tag>.FromJson(nativeTypeName, value)
        NativeCodec<NativeEnumValue<'Tag>>((fun source -> read (source.ReadJson())),
            (fun value ->
                if value.NativeTypeName <> nativeTypeName then NativeTypedJson.fail "type_mismatch" "The native enum belongs to a different type."
                value.Value),
            decodeKey = (fun key ->
                let mutable number = 0L
                let mutable unsigned = 0UL
                if Int64.TryParse(key, NumberStyles.Integer, CultureInfo.InvariantCulture, &number) then read (NativeTypedJson.element number)
                elif UInt64.TryParse(key, NumberStyles.Integer, CultureInfo.InvariantCulture, &unsigned) then read (NativeTypedJson.element unsigned)
                else read (NativeTypedJson.element key)))
    static member View<'T when 'T :> NativeTypeView>(factory: NativeTypeView -> 'T) =
        NativeCodec<'T>(factory, fun value -> value.ReadJson())
    static member SnapshotView<'T when 'T :> NativeTypeView>(factory: NativeTypeView -> 'T) =
        NativeCodec<'T>((fun source -> factory (NativeTypeView.FromSnapshot(source.ReadJson()))), fun value -> value.ReadJson())
    static member Array(codec: NativeCodec<'T>) =
        NativeCodec<NativeArray<'T>>((fun source -> NativeArray<'T>(source, codec)), fun value -> value.ReadJson())
    static member Sequence(codec: NativeCodec<'T>) = NativeCodecs.Array(codec)
    static member Map(keyCodec: NativeCodec<'Key>, valueCodec: NativeCodec<'Value>) =
        NativeCodec<NativeMap<'Key, 'Value>>((fun source -> NativeMap<'Key, 'Value>(source, keyCodec, valueCodec)), fun value -> value.ReadJson())
    static member Reference(codec: NativeCodec<'T>) =
        NativeCodec<NativeReference<'T>>((fun source -> NativeReference<'T>(source, codec)), fun value -> value.ReadJson())
    static member Optional(codec: NativeCodec<'T>) =
        NativeCodec<NativeOptional<'T>>((fun source -> NativeOptional<'T>(source, codec)), fun value -> value.ReadJson())
    static member Expected(valueCodec: NativeCodec<'T>, errorCodec: NativeCodec<'Error>) =
        NativeCodec<NativeExpected<'T, 'Error>>((fun source -> NativeExpected<'T, 'Error>(source, valueCodec, errorCodec)), fun value -> value.ReadJson())
    static member Float2 =
        NativeCodec<Float2>((fun source ->
            let value = source.ReadJson()
            Float2(value.GetProperty("x").GetSingle(), value.GetProperty("y").GetSingle())),
            fun value -> NativeTypedJson.element (dict [ "x", value.X; "y", value.Y ]))
    static member Float3 =
        NativeCodec<Float3>((fun source ->
            let value = source.ReadJson()
            Float3(value.GetProperty("x").GetSingle(), value.GetProperty("y").GetSingle(), value.GetProperty("z").GetSingle())),
            fun value -> NativeTypedJson.element (dict [ "x", value.X; "y", value.Y; "z", value.Z ]))
    static member Quaternion =
        NativeCodec<Quaternion>((fun source ->
            let value = source.ReadJson()
            Quaternion(value.GetProperty("x").GetSingle(), value.GetProperty("y").GetSingle(), value.GetProperty("z").GetSingle(), value.GetProperty("w").GetSingle())),
            fun value -> NativeTypedJson.element (dict [ "x", value.X; "y", value.Y; "z", value.Z; "w", value.W ]))
    static member Color4 =
        NativeCodec<Color4>((fun source ->
            let value = source.ReadJson()
            Color4(value.GetProperty("r").GetSingle(), value.GetProperty("g").GetSingle(), value.GetProperty("b").GetSingle(), value.GetProperty("a").GetSingle())),
            fun value -> NativeTypedJson.element (dict [ "r", value.R; "g", value.G; "b", value.B; "a", value.A ]))
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>) =
        NativeCodec<NativeTuple2<'T1, 'T2>>((fun source -> NativeTuple2<'T1, 'T2>(source, codec1, codec2)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>) =
        NativeCodec<NativeTuple3<'T1, 'T2, 'T3>>((fun source -> NativeTuple3<'T1, 'T2, 'T3>(source, codec1, codec2, codec3)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>) =
        NativeCodec<NativeTuple4<'T1, 'T2, 'T3, 'T4>>((fun source -> NativeTuple4<'T1, 'T2, 'T3, 'T4>(source, codec1, codec2, codec3, codec4)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>) =
        NativeCodec<NativeTuple5<'T1, 'T2, 'T3, 'T4, 'T5>>((fun source -> NativeTuple5<'T1, 'T2, 'T3, 'T4, 'T5>(source, codec1, codec2, codec3, codec4, codec5)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>) =
        NativeCodec<NativeTuple6<'T1, 'T2, 'T3, 'T4, 'T5, 'T6>>((fun source -> NativeTuple6<'T1, 'T2, 'T3, 'T4, 'T5, 'T6>(source, codec1, codec2, codec3, codec4, codec5, codec6)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>) =
        NativeCodec<NativeTuple7<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7>>((fun source -> NativeTuple7<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7>(source, codec1, codec2, codec3, codec4, codec5, codec6, codec7)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>) =
        NativeCodec<NativeTuple8<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8>>((fun source -> NativeTuple8<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8>(source, codec1, codec2, codec3, codec4, codec5, codec6, codec7, codec8)), fun value -> value.ReadJson())
    static member Tuple(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>, codec9: NativeCodec<'T9>) =
        NativeCodec<NativeTuple9<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8, 'T9>>((fun source -> NativeTuple9<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8, 'T9>(source, codec1, codec2, codec3, codec4, codec5, codec6, codec7, codec8, codec9)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>) =
        NativeCodec<NativeVariant2<'T1, 'T2>>((fun source -> NativeVariant2<'T1, 'T2>(source, codec1, codec2)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>) =
        NativeCodec<NativeVariant3<'T1, 'T2, 'T3>>((fun source -> NativeVariant3<'T1, 'T2, 'T3>(source, codec1, codec2, codec3)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>) =
        NativeCodec<NativeVariant4<'T1, 'T2, 'T3, 'T4>>((fun source -> NativeVariant4<'T1, 'T2, 'T3, 'T4>(source, codec1, codec2, codec3, codec4)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>) =
        NativeCodec<NativeVariant5<'T1, 'T2, 'T3, 'T4, 'T5>>((fun source -> NativeVariant5<'T1, 'T2, 'T3, 'T4, 'T5>(source, codec1, codec2, codec3, codec4, codec5)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>) =
        NativeCodec<NativeVariant6<'T1, 'T2, 'T3, 'T4, 'T5, 'T6>>((fun source -> NativeVariant6<'T1, 'T2, 'T3, 'T4, 'T5, 'T6>(source, codec1, codec2, codec3, codec4, codec5, codec6)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>) =
        NativeCodec<NativeVariant7<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7>>((fun source -> NativeVariant7<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7>(source, codec1, codec2, codec3, codec4, codec5, codec6, codec7)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>) =
        NativeCodec<NativeVariant8<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8>>((fun source -> NativeVariant8<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8>(source, codec1, codec2, codec3, codec4, codec5, codec6, codec7, codec8)), fun value -> value.ReadJson())
    static member Variant(codec1: NativeCodec<'T1>, codec2: NativeCodec<'T2>, codec3: NativeCodec<'T3>, codec4: NativeCodec<'T4>, codec5: NativeCodec<'T5>, codec6: NativeCodec<'T6>, codec7: NativeCodec<'T7>, codec8: NativeCodec<'T8>, codec9: NativeCodec<'T9>) =
        NativeCodec<NativeVariant9<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8, 'T9>>((fun source -> NativeVariant9<'T1, 'T2, 'T3, 'T4, 'T5, 'T6, 'T7, 'T8, 'T9>(source, codec1, codec2, codec3, codec4, codec5, codec6, codec7, codec8, codec9)), fun value -> value.ReadJson())
