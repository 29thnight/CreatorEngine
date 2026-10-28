namespace CreatorEngine

/// A native placeholder whose managed script could not be loaded.
[<AllowNullLiteral; Sealed>]
type InvalidScriptComponent internal (source: NativeObject) =
    inherit ReflectedNativeComponent(source)

    member _.ErrorMessage = source.["m_errorMessage"].Read<string>()

    static member Find(owner: Entity) =
        match NativeReflection.TryGetComponent(owner.Handle, "InvalidScriptComponent") with
        | Some source -> InvalidScriptComponent(source)
        | None -> null
