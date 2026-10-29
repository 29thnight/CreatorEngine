namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type SoundComponent internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.Play() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_Play) -> api.Sound_Play.Invoke(this.OwnerHandle.ToNative())
        | _ -> ()
    member this.Stop() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_Stop) -> api.Sound_Stop.Invoke(this.OwnerHandle.ToNative())
        | _ -> ()
    member this.Pause(pause: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_Pause) -> api.Sound_Pause.Invoke(this.OwnerHandle.ToNative(), if pause then 1 else 0)
        | _ -> ()
    member this.IsPlaying() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_IsPlaying) -> api.Sound_IsPlaying.Invoke(this.OwnerHandle.ToNative()) <> 0
        | _ -> false
    member this.PlayOneShot() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_PlayOneShot) -> api.Sound_PlayOneShot.Invoke(this.OwnerHandle.ToNative())
        | _ -> ()
    member this.PlayInstance() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_PlayInstance) -> PlaybackHandle(api.Sound_PlayInstance.Invoke(this.OwnerHandle.ToNative(), 0))
        | _ -> PlaybackHandle(0UL)
    member this.PlayOneShotInstance() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Sound_PlayInstance) -> PlaybackHandle(api.Sound_PlayInstance.Invoke(this.OwnerHandle.ToNative(), 1))
        | _ -> PlaybackHandle(0UL)
    member this.ClipKey
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_GetClipKey) ->
                let buffer = NativePtr.stackalloc<byte> 256
                let length = api.Sound_GetClipKey.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer, 256)
                if length > 0 then Encoding.UTF8.GetString(ReadOnlySpan<byte>(NativePtr.toVoidPtr buffer, length)) else String.Empty
            | _ -> String.Empty
        and set (value: string) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_SetClipKey) ->
                let buffer = NativePtr.stackalloc<byte> 256
                let length = Encoding.UTF8.GetBytes(value.AsSpan(), Span<byte>(NativePtr.toVoidPtr buffer, 255))
                NativePtr.set buffer length 0uy
                api.Sound_SetClipKey.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt buffer)
            | _ -> ()
    member this.Source
        with get () =
            let mutable asset = Unchecked.defaultof<AudioAssetId>
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_GetSource) ->
                use pointer = fixed &asset
                let kind = enum<AudioSourceKind>(api.Sound_GetSource.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt pointer))
                AudioSource(asset, kind)
            | _ -> AudioSource(asset)
        and set (value: AudioSource) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_SetSource) -> api.Sound_SetSource.Invoke(this.OwnerHandle.ToNative(), value.Asset.ToNative(), int value.Kind)
            | _ -> ()
    member this.Settings
        with get () =
            let mutable settings = AudioPlaySettings.Default
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_GetSettings) ->
                use pointer = fixed &settings
                api.Sound_GetSettings.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt pointer)
            | _ -> ()
            settings
        and set (value: AudioPlaySettings) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_SetSettings) ->
                let mutable settings = value
                use pointer = fixed &settings
                api.Sound_SetSettings.Invoke(this.OwnerHandle.ToNative(), NativePtr.toNativeInt pointer)
            | _ -> ()
    member this.Volume
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_GetVolume) -> api.Sound_GetVolume.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_SetVolume) -> api.Sound_SetVolume.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()
    member this.Pitch
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_GetPitch) -> api.Sound_GetPitch.Invoke(this.OwnerHandle.ToNative())
            | _ -> 0.0f
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_SetPitch) -> api.Sound_SetPitch.Invoke(this.OwnerHandle.ToNative(), value)
            | _ -> ()

[<AllowNullLiteral; Sealed>]
type AudioListenerComponent internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.Active
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.AudioListener_GetActive) -> api.AudioListener_GetActive.Invoke(this.OwnerHandle.ToNative()) <> 0
            | _ -> false
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.AudioListener_SetActive) -> api.AudioListener_SetActive.Invoke(this.OwnerHandle.ToNative(), if value then 1 else 0)
            | _ -> ()

[<AbstractClass; Sealed>]
type internal SoundBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<SoundComponent>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Sound_Exists) && api.Sound_Exists.Invoke(h.ToNative()) <> 0 -> SoundComponent(h) :> Component
            | _ -> null))
        Component.RegisterNative(typeof<AudioListenerComponent>, Func<ObjectHandle, Component>(fun h ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.AudioListener_Exists) && api.AudioListener_Exists.Invoke(h.ToNative()) <> 0 -> AudioListenerComponent(h) :> Component
            | _ -> null))
