namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AbstractClass; Sealed>]
type internal UIBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<RectTransformComponent>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Rect_Exists) && api.Rect_Exists.Invoke(h.ToNative()) <> 0 -> RectTransformComponent(h) :> Component
            | _ -> null))
        Component.RegisterNative(typeof<ImageComponent>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Image_Exists) && api.Image_Exists.Invoke(h.ToNative()) <> 0 -> ImageComponent(h) :> Component
            | _ -> null))
        Component.RegisterNative(typeof<TextComponent>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Text_Exists) && api.Text_Exists.Invoke(h.ToNative()) <> 0 -> TextComponent(h) :> Component
            | _ -> null))
        Component.RegisterNative(typeof<Canvas>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Canvas_Exists) && api.Canvas_Exists.Invoke(h.ToNative()) <> 0 -> Canvas(h) :> Component
            | _ -> null))
        Component.RegisterNative(typeof<UIButton>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Button_Exists) && api.Button_Exists.Invoke(h.ToNative()) <> 0 -> UIButton(h) :> Component
            | _ -> null))
