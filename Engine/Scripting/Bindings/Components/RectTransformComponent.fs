namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type RectTransformComponent internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.AnchoredPosition
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_GetAnchoredPosition) -> Float2.FromNative(api.Rect_GetAnchoredPosition.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float2.Zero
        and set (value: Float2) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_SetAnchoredPosition) -> api.Rect_SetAnchoredPosition.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.ScreenPosition
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_GetScreenPosition) -> Float2.FromNative(api.Rect_GetScreenPosition.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float2.Zero
        and set (value: Float2) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_SetScreenPosition) -> api.Rect_SetScreenPosition.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.SizeDelta
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_GetSizeDelta) -> Float2.FromNative(api.Rect_GetSizeDelta.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float2.Zero
        and set (value: Float2) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_SetSizeDelta) -> api.Rect_SetSizeDelta.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.Pivot
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_GetPivot) -> Float2.FromNative(api.Rect_GetPivot.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float2.Zero
        and set (value: Float2) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_SetPivot) -> api.Rect_SetPivot.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.WorldRect =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Rect_GetWorldRect) ->
            let value = api.Rect_GetWorldRect.Invoke(this.OwnerHandle.ToNative())
            struct (value.x, value.y, value.z, value.w)
        | _ -> struct (0.0f, 0.0f, 0.0f, 0.0f)
    member this.SetAnchoredPosition(value: Float2) = this.AnchoredPosition <- value
