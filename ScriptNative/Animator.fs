namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type Animator internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    static member private EncodeName(name: string, pointer: nativeptr<byte>) =
        let bytes = Span<byte>(NativePtr.toVoidPtr pointer, 127)
        let length = Encoding.UTF8.GetBytes(name.AsSpan(), bytes)
        NativePtr.set pointer length 0uy
    member this.SetBool(name: string, value: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_SetBool) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_SetBool.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p, if value then 1 else 0)
        | _ -> ()
    member this.SetFloat(name: string, value: single) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_SetFloat) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_SetFloat.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p, value)
        | _ -> ()
    member this.SetInt(name: string, value: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_SetInt) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_SetInt.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p, value)
        | _ -> ()
    member this.SetTrigger(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_SetTrigger) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_SetTrigger.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p)
        | _ -> ()
    member this.ResetTrigger(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_ResetTrigger) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_ResetTrigger.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p)
        | _ -> ()
    member this.GetBool(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_GetBool) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_GetBool.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p) <> 0
        | _ -> false
    member this.GetFloat(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_GetFloat) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_GetFloat.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p)
        | _ -> 0.0f
    member this.GetInt(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_GetInt) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_GetInt.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p)
        | _ -> 0
    member this.HasParameter(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_HasParameter) ->
            let p = NativePtr.stackalloc<byte> 128
            Animator.EncodeName(name, p)
            api.Animator_HasParameter.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt p) <> 0
        | _ -> false
    member this.SetUseLayer(index: int, useLayer: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_SetUseLayer) -> api.Animator_SetUseLayer.Invoke(this.OwnerHandle.ToNative(), index, if useLayer then 1 else 0)
        | _ -> ()
    member this.StopAnimation(duration: single) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Animator_StopAnimation) -> api.Animator_StopAnimation.Invoke(this.OwnerHandle.ToNative(), duration)
        | _ -> ()

[<AbstractClass; Sealed>]
type internal AnimatorBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<Animator>, Func<ObjectHandle, Component>(fun owner ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Animator_Exists) && api.Animator_Exists.Invoke(owner.ToNative()) <> 0 -> Animator(owner) :> Component
            | _ -> null))
