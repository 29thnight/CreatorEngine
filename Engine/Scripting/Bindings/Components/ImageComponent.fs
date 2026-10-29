namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type ImageComponent internal (owner: ObjectHandle) =
    inherit UIComponent(owner)
    member this.SetTexture(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Image_SetTexture) -> api.Image_SetTexture.Invoke(this.OwnerHandle.ToNative(), index)
        | _ -> ()
    member this.TextureCount =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Image_GetTextureCount) -> api.Image_GetTextureCount.Invoke(this.OwnerHandle.ToNative())
        | _ -> 0
    member this.TextureIndex =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Image_GetTextureIndex) -> api.Image_GetTextureIndex.Invoke(this.OwnerHandle.ToNative())
        | _ -> 0
    member this.Color
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_GetColor) -> Color4.FromNative(api.Image_GetColor.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Color4.White
        and set (value: Color4) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_SetColor) -> api.Image_SetColor.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.ClipPercent
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_GetClipPercent) -> api.Image_GetClipPercent.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_SetClipPercent) -> api.Image_SetClipPercent.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.Rotation
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_GetRotation) -> api.Image_GetRotation.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_SetRotation) -> api.Image_SetRotation.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.SetNativeSize() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Image_SetNativeSize) -> api.Image_SetNativeSize.Invoke(this.OwnerHandle.ToNative())
        | _ -> ()
