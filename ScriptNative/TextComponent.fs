namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type TextComponent internal (owner: ObjectHandle) =
    inherit UIComponent(owner)
    member this.Message
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_GetMessage) ->
                let buffer = NativePtr.stackalloc<byte> 1024
                let length = api.Text_GetMessage.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer, 1024)
                if length > 0 then Encoding.UTF8.GetString(ReadOnlySpan<byte>(NativePtr.toVoidPtr buffer, length)) else String.Empty
            | _ -> String.Empty
        and set (value: string) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_SetMessage) ->
                let buffer = NativePtr.stackalloc<byte> 1024
                let length = Encoding.UTF8.GetBytes(value.AsSpan(), Span<byte>(NativePtr.toVoidPtr buffer, 1023))
                NativePtr.set buffer length 0uy
                api.Text_SetMessage.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer)
            | _ -> ()
    member this.Color
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_GetColor) -> Color4.FromNative(api.Text_GetColor.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Color4.White
        and set (value: Color4) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_SetColor) -> api.Text_SetColor.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.Alpha
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_GetAlpha) -> api.Text_GetAlpha.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_SetAlpha) -> api.Text_SetAlpha.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.FontSize
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_GetFontSize) -> api.Text_GetFontSize.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_SetFontSize) -> api.Text_SetFontSize.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.RelativePosition
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_GetRelativePosition) -> Float2.FromNative(api.Text_GetRelativePosition.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float2.Zero
        and set (value: Float2) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_SetRelativePosition) -> api.Text_SetRelativePosition.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
