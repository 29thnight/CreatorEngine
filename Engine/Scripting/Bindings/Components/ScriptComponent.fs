namespace CreatorEngine

/// The native host component for a managed script instance.
[<AllowNullLiteral; Sealed>]
type ScriptComponent internal (source: NativeObject) =
    inherit ReflectedNativeComponent(source)

    member _.ScriptType
        with get () = source.["m_scriptType"].Read<string>()
        and set (value: string) = source.["m_scriptType"].Write(value)

    member _.FieldData
        with get () = source.["m_fieldData"].Read<string array>()
        and set (value: string array) = source.["m_fieldData"].Write(value)

    member _.ManagedInstanceId = source.Invoke("GetInstanceId", [||]).Read<int>()
    member _.HasInstance = source.Invoke("HasInstance", [||]).Read<bool>()
    member _.InitializationAttempts = source.Invoke("InitializationAttempts", [||]).Read<uint64>()

    static member Find(owner: Entity) =
        match NativeReflection.TryGetComponent(owner.Handle, "ScriptComponent") with
        | Some source -> ScriptComponent(source)
        | None -> null
