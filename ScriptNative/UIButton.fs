namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type UIButton internal (owner: ObjectHandle) =
    inherit UIComponent(owner)
    member this.WasClicked() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Button_ConsumeClicked) -> api.Button_ConsumeClicked.Invoke(this.OwnerHandle.ToNative()) <> 0
        | _ -> false
