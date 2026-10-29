namespace CreatorEngine

/// A native component whose complete API is supplied by the reflection bridge.
/// The concrete native instance is captured when this wrapper is created.
[<AllowNullLiteral; AbstractClass>]
type ReflectedNativeComponent internal (source: NativeObject) =
    inherit NativeComponent(source.OwnerHandle, nativeInstance = source.InstanceId)

    member internal _.ReflectionSource = source

    member _.IsPresent =
        try
            source.Describe() |> ignore
            true
        with
        | :? NativeReflectionException as error when
            error.Code = "stale_owner" || error.Code = "stale_component" ||
            error.Code = "component_not_found" -> false

    override _.Enabled
        with get () = source.Invoke("IsEnabled", [||]).Read<bool>()
        and set value = source.Invoke("SetEnabled", [| box value |]) |> ignore
