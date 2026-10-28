namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type Canvas internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.Order
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Canvas_GetOrder) -> api.Canvas_GetOrder.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Canvas_SetOrder) -> api.Canvas_SetOrder.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.Name
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Canvas_GetName) ->
                let buffer = NativePtr.stackalloc<byte> 128
                let length = api.Canvas_GetName.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer, 128)
                if length > 0 then Encoding.UTF8.GetString(ReadOnlySpan<byte>(NativePtr.toVoidPtr buffer, length)) else String.Empty
            | _ -> String.Empty
        and set (value: string) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Canvas_SetName) ->
                let buffer = NativePtr.stackalloc<byte> 128
                let length = Encoding.UTF8.GetBytes(value.AsSpan(), Span<byte>(NativePtr.toVoidPtr buffer, 127))
                NativePtr.set buffer length 0uy
                api.Canvas_SetName.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer)
            | _ -> ()
