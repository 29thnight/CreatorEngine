namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AbstractClass; Sealed>]
type UINavigation private () =
    static member Selected
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.UiNav_GetSelected) -> Entity(ObjectHandle.FromNative(api.UiNav_GetSelected.Invoke()))
            | _ -> Unchecked.defaultof<Entity>
        and set (value: Entity) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.UiNav_SetSelected) -> api.UiNav_SetSelected.Invoke(value.Handle.ToNative())
            | _ -> ()
