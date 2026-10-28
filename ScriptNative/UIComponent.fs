namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; AbstractClass>]
type UIComponent internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.Order
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Ui_GetOrder) -> api.Ui_GetOrder.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Ui_SetOrder) -> api.Ui_SetOrder.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.IsSelected =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Ui_IsSelected) -> api.Ui_IsSelected.Invoke(this.OwnerHandle.ToNative()) <> 0
        | _ -> false
    member this.NavLock
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Ui_IsNavLocked) -> api.Ui_IsNavLocked.Invoke(this.OwnerHandle.ToNative()) <> 0
            | _ -> false
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Ui_SetNavLock) -> api.Ui_SetNavLock.Invoke(this.OwnerHandle.ToNative(), if value then 1 else 0)
            | _ -> ()
