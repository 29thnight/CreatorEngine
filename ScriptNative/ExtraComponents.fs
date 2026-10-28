namespace CreatorEngine

open System
open CreatorEngine.Native

[<AllowNullLiteral; AbstractClass>]
type ExtraNativeComponent internal (owner: ObjectHandle, kind: int) =
    inherit NativeComponent(owner)
    member this.IsPresent =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.ExtraComponent_Exists) -> api.ExtraComponent_Exists.Invoke(this.OwnerHandle.ToNative(), kind) <> 0
        | _ -> false
    override this.Enabled
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.ExtraComponent_GetEnabled) -> api.ExtraComponent_GetEnabled.Invoke(this.OwnerHandle.ToNative(), kind) <> 0
            | _ -> false
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.ExtraComponent_SetEnabled) -> api.ExtraComponent_SetEnabled.Invoke(this.OwnerHandle.ToNative(), kind, if value then 1 else 0)
            | _ -> ()
