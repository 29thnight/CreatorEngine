namespace CreatorEngine

[<AutoOpen>]
module NativeReflectionExtensions =
    type Entity with
        member this.NativeComponents = NativeReflection.GetComponents(this.Handle)
        member this.TryGetNativeComponent(typeName: string) = NativeReflection.TryGetComponent(this.Handle, typeName)
        member this.GetNativeComponent(typeName: string) = NativeReflection.GetComponent(this.Handle, typeName)

    type NativeComponent with
        /// Resolves the complete reflected surface of this wrapper's native component.
        member this.Reflected =
            let value = NativeReflection.GetComponent(this.OwnerHandle, this.GetType().Name)
            match this.NativeInstanceId with
            | Some expected when value.InstanceId <> expected ->
                raise (NativeReflectionException("stale_component", "The native component wrapper refers to a removed instance."))
            | _ -> value
