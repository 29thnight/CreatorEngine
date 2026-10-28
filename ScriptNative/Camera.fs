namespace CreatorEngine

open System
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type CameraComponent internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.NotifyCameraCut() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Camera_NotifyCameraCut) -> api.Camera_NotifyCameraCut.Invoke(this.OwnerHandle.ToNative())
        | _ -> ()
    member this.FieldOfView
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_GetFov) -> api.Camera_GetFov.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_SetFov) -> api.Camera_SetFov.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.NearPlane
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_GetNearPlane) -> api.Camera_GetNearPlane.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_SetNearPlane) -> api.Camera_SetNearPlane.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.FarPlane
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_GetFarPlane) -> api.Camera_GetFarPlane.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_SetFarPlane) -> api.Camera_SetFarPlane.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.IsPrimary
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_IsPrimary) -> api.Camera_IsPrimary.Invoke(this.OwnerHandle.ToNative()) <> 0
            | _ -> false
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_SetPrimary) -> api.Camera_SetPrimary.Invoke(this.OwnerHandle.ToNative(), if value then 1 else 0)
            | _ -> ()

[<AbstractClass; Sealed>]
type Camera private () =
    static member Exists =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Camera_PrimaryExists) -> api.Camera_PrimaryExists.Invoke() <> 0
        | _ -> false
    static member ScreenSize =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Camera_GetScreenSize) -> Float2.FromNative(api.Camera_GetScreenSize.Invoke())
        | _ -> Float2.Zero
    static member WorldToScreenPoint(world: Float3) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Camera_WorldToScreenPoint) -> Float3.FromNative(api.Camera_WorldToScreenPoint.Invoke(world.ToNative()))
        | _ -> Float3.Zero
    static member Main =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Camera_GetPrimaryHandle) ->
            let owner = Entity(ObjectHandle.FromNative(api.Camera_GetPrimaryHandle.Invoke()))
            if owner.IsAlive then owner.GetComponent<CameraComponent>() else null
        | _ -> null

[<AbstractClass; Sealed>]
type internal CameraBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<CameraComponent>, Func<ObjectHandle, Component>(fun owner ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Camera_Exists) && api.Camera_Exists.Invoke(owner.ToNative()) <> 0 -> CameraComponent(owner) :> Component
            | _ -> null))
