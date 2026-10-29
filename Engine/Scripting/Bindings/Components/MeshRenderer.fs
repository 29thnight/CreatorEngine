namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type MeshRenderer internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    static member private Encode(name: string, buffer: nativeptr<byte>) =
        let length = Encoding.UTF8.GetBytes(name.AsSpan(), Span<byte>(NativePtr.toVoidPtr buffer, 127))
        NativePtr.set buffer length 0uy
    member this.MaterialName =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Mesh_GetMaterialName) ->
            let buffer = NativePtr.stackalloc<byte> 128
            let length = api.Mesh_GetMaterialName.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer, 128)
            if length > 0 then Encoding.UTF8.GetString(ReadOnlySpan<byte>(NativePtr.toVoidPtr buffer, length)) else String.Empty
        | _ -> String.Empty
    member this.InstantiateMaterial(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Mesh_InstantiateMaterial) ->
            let buffer = NativePtr.stackalloc<byte> 128
            MeshRenderer.Encode(name, buffer)
            api.Mesh_InstantiateMaterial.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer)
        | _ -> ()
    member this.SetMaterialFloat(name: string, value: single) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Mesh_SetMaterialFloat) ->
            let buffer = NativePtr.stackalloc<byte> 128
            MeshRenderer.Encode(name, buffer)
            api.Mesh_SetMaterialFloat.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer, value) <> 0
        | _ -> false
    member this.SetMaterialInt(name: string, value: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Mesh_SetMaterialInt) ->
            let buffer = NativePtr.stackalloc<byte> 128
            MeshRenderer.Encode(name, buffer)
            api.Mesh_SetMaterialInt.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer, value) <> 0
        | _ -> false
    member this.BaseColor
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Mesh_GetBaseColor) -> Color4.FromNative(api.Mesh_GetBaseColor.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Color4.White
        and set (value: Color4) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Mesh_SetBaseColor) -> api.Mesh_SetBaseColor.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()

[<AbstractClass; Sealed>]
type internal MeshBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<MeshRenderer>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Mesh_Exists) && api.Mesh_Exists.Invoke(h.ToNative()) <> 0 -> MeshRenderer(h) :> Component
            | _ -> null))
