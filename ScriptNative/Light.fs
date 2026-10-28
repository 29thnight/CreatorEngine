namespace CreatorEngine

open System
open CreatorEngine.Native

type LightType = | Directional = 0 | Point = 1 | Spot = 2
type LightStatus = | Disabled = 0 | Enabled = 1 | StaticShadows = 2

[<AllowNullLiteral; Sealed>]
type LightComponent internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.Color
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_GetColor) -> Color4.FromNative(api.Light_GetColor.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Color4.White
        and set (value: Color4) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_SetColor) -> api.Light_SetColor.Invoke(this.OwnerHandle.ToNative(), value.ToNative())
            | _ -> ()
    member this.Intensity
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_GetIntensity) -> api.Light_GetIntensity.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_SetIntensity) -> api.Light_SetIntensity.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.Range
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_GetRange) -> api.Light_GetRange.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_SetRange) -> api.Light_SetRange.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.SpotAngle
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_GetSpotAngle) -> api.Light_GetSpotAngle.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_SetSpotAngle) -> api.Light_SetSpotAngle.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.Type
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_GetLightType) -> enum<LightType>(api.Light_GetLightType.Invoke(this.OwnerHandle.ToNative()))
            | _ -> LightType.Directional
        and set (value: LightType) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_SetLightType) -> api.Light_SetLightType.Invoke(this.OwnerHandle.ToNative(), int value)
            | _ -> ()
    member this.Status
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_GetLightStatus) -> enum<LightStatus>(api.Light_GetLightStatus.Invoke(this.OwnerHandle.ToNative()))
            | _ -> LightStatus.Disabled
        and set (value: LightStatus) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_SetLightStatus) -> api.Light_SetLightStatus.Invoke(this.OwnerHandle.ToNative(), int value)
            | _ -> ()

[<AbstractClass; Sealed>]
type internal LightBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<LightComponent>, Func<ObjectHandle, Component>(fun owner ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Light_Exists) && api.Light_Exists.Invoke(owner.ToNative()) <> 0 -> LightComponent(owner) :> Component
            | _ -> null))
