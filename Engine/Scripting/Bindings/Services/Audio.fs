namespace CreatorEngine

#nowarn "9"

open System
open System.Buffers.Binary
open System.Collections.Generic
open System.Runtime.InteropServices
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native
open CreatorEngine.Native.reflgen_creator_script_native

type AudioSourceKind = Clip = 0 | Preset = 1 | Graph = 2
type AudioPlaybackState = Stopped = 0 | Playing = 1 | Paused = 2
type AudioStealPolicy = Reject = 0 | Oldest = 1 | Quietest = 2 | LowestPriority = 3
type AudioReverbPreset = Off = 0 | Room = 1 | Hall = 2
type AudioPlaybackEndReason = Finished = 0 | Stopped = 1 | ScopeEnded = 2 | OwnerDestroyed = 3 | Replaced = 4
type AudioParameterType = Boolean = 0 | Integer = 1 | Float = 2 | String = 3

[<Struct; StructLayout(LayoutKind.Sequential); CustomEquality; NoComparison>]
type AudioAssetId =
    val First: uint64
    val Second: uint64
    new (guid: Guid) =
        let bytes = guid.ToByteArray(true)
        { First = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(0, 8))
          Second = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(8, 8)) }
    static member Parse(text: string) = AudioAssetId(Guid.Parse(text))
    member this.IsValid = this.First <> 0UL || this.Second <> 0UL
    member internal this.ToNative() =
        let mutable value = Unchecked.defaultof<ce_script_binding_AudioAssetId>
        value.first <- this.First; value.second <- this.Second
        value
    override this.Equals(other) =
        match other with
        | :? AudioAssetId as value -> this.First = value.First && this.Second = value.Second
        | _ -> false
    override this.GetHashCode() = HashCode.Combine(this.First, this.Second)
    interface IEquatable<AudioAssetId> with member this.Equals(value) = this.First = value.First && this.Second = value.Second
    override this.ToString() =
        let bytes = Array.zeroCreate<byte> 16
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(0, 8), this.First)
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(8, 8), this.Second)
        Guid(ReadOnlySpan<byte>(bytes), true).ToString()

[<Struct>]
type AudioParameter =
    val Name: string
    val Type: AudioParameterType
    val internal Integer: int
    val internal Number: single
    val internal Text: string
    new (name, value: bool) = { Name = name; Type = AudioParameterType.Boolean; Integer = (if value then 1 else 0); Number = 0.0f; Text = null }
    new (name, value: int) = { Name = name; Type = AudioParameterType.Integer; Integer = value; Number = 0.0f; Text = null }
    new (name, value: single) = { Name = name; Type = AudioParameterType.Float; Integer = 0; Number = value; Text = null }
    new (name, value: string) =
        if isNull value then nullArg "value"
        { Name = name; Type = AudioParameterType.String; Integer = 0; Number = 0.0f; Text = value }

[<Struct>]
type AudioSource =
    val Asset: AudioAssetId
    val Kind: AudioSourceKind
    new (asset) = { Asset = asset; Kind = AudioSourceKind.Clip }
    new (asset, kind) = { Asset = asset; Kind = kind }

[<Struct>]
type AudioScope =
    val Value: uint64
    new (value) = { Value = value }
    member this.IsValid = (this.Value >>> 32) <> 0UL

module internal AudioSettingsConversion =
    let toNative (settings: AudioPlaySettings) =
        let mutable value = Unchecked.defaultof<ce_script_binding_AudioPlaySettings>
        value.volume <- settings.Volume; value.pitch <- settings.Pitch; value.priority <- settings.Priority; value.loop <- settings.Loop
        value.bus <- int settings.Bus; value.spatialBlend <- settings.SpatialBlend; value.minimumDistance <- settings.MinimumDistance
        value.maximumDistance <- settings.MaximumDistance; value.rolloff <- int settings.Rolloff; value.useReverbSend <- settings.UseReverbSend
        value.reverbSendDecibels <- settings.ReverbSendDecibels; value.ownerPolicy <- int settings.OwnerDestroyedPolicy
        value.concurrencyGroup <- settings.ConcurrencyGroup; value.preemptSameClip <- settings.PreemptSameClip; value.allowVirtualization <- settings.AllowVirtualization
        value

[<Struct; CustomEquality; NoComparison>]
type PlaybackHandle =
    val Value: uint64
    new (value) = { Value = value }
    member this.IsValid = (this.Value >>> 32) <> 0UL
    member this.State =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_State) -> enum<AudioPlaybackState>(api.Audio_State.Invoke(this.Value))
        | _ -> AudioPlaybackState.Stopped
    member this.IsPlaying = this.State <> AudioPlaybackState.Stopped
    member private this.Control(operation, gain, pitch) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_Control) -> api.Audio_Control.Invoke(this.Value, operation, gain, pitch)
        | _ -> ()
    member this.Stop() = this.Control(0, 0.0f, 0.0f)
    member this.Pause() = this.Control(1, 0.0f, 0.0f)
    member this.Resume() = this.Control(2, 0.0f, 0.0f)
    member this.SetGainPitch(gain: single, pitch: single) = this.Control(3, gain, pitch)
    member this.SetTransform(position: Float3, velocity: Float3) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_SetTransform) -> api.Audio_SetTransform.Invoke(this.Value, position.ToNative(), velocity.ToNative())
        | _ -> ()
    member private this.SetParameterCore(name: string, kind: int, integer: int, number: single, text: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_SetParameter) ->
            if String.IsNullOrEmpty(name) || name.Contains('\000') || Encoding.UTF8.GetByteCount(name) > 256 ||
               (not (isNull text) && (text.Contains('\000') || Encoding.UTF8.GetByteCount(text) > 4096)) then
                invalidArg "name" "Audio parameter exceeds its UTF-8 size limit."
            let namePointer = Marshal.StringToCoTaskMemUTF8(name)
            let mutable textPointer = 0n
            try
                textPointer <- Marshal.StringToCoTaskMemUTF8(if isNull text then String.Empty else text)
                api.Audio_SetParameter.Invoke(this.Value, namePointer, kind, integer, number, textPointer) <> 0
            finally
                Marshal.FreeCoTaskMem(namePointer)
                Marshal.FreeCoTaskMem(textPointer)
        | _ -> false
    member this.SetParameter(name: string, value: bool) = this.SetParameterCore(name, 0, (if value then 1 else 0), 0.0f, null)
    member this.SetParameter(name: string, value: int) = this.SetParameterCore(name, 1, value, 0.0f, null)
    member this.SetParameter(name: string, value: single) = this.SetParameterCore(name, 2, 0, value, null)
    member this.SetParameter(name: string, value: string) = this.SetParameterCore(name, 3, 0, 0.0f, value)
    override this.Equals(other) = match other with | :? PlaybackHandle as value -> this.Value = value.Value | _ -> false
    override this.GetHashCode() = this.Value.GetHashCode()
    interface IEquatable<PlaybackHandle> with member this.Equals(value) = this.Value = value.Value

[<Struct; StructLayout(LayoutKind.Sequential)>]
type AudioPlaybackCompletion =
    val mutable PlaybackValue: uint64
    val mutable ScopeValue: uint64
    val mutable OwnerId: uint64
    val mutable Reason: AudioPlaybackEndReason
    member this.Playback = PlaybackHandle(this.PlaybackValue)
    member this.Scope = AudioScope(this.ScopeValue)

[<AbstractClass; Sealed>]
type Audio private () =
    static member private Configure(id: uint32, cap: int, policy: AudioStealPolicy, group: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_Configure) -> api.Audio_Configure.Invoke(id, cap, int policy, if group then 1 else 0) <> 0
        | _ -> false
    static member ConfigureBus(bus: AudioBus, cap: int, policy: AudioStealPolicy) = Audio.Configure(uint32 bus, cap, policy, false)
    static member ConfigureMaster(cap: int, policy: AudioStealPolicy) = Audio.Configure(5u, cap, policy, false)
    static member ConfigureConcurrencyGroup(group: uint32, cap: int, policy: AudioStealPolicy) = Audio.Configure(group, cap, policy, true)
    static member SetReverbPreset(preset: AudioReverbPreset) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_SetReverbPreset) -> api.Audio_SetReverbPreset.Invoke(int preset) <> 0
        | _ -> false
    static member LastError =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_LastError) ->
            let pointer = Marshal.AllocHGlobal(2048)
            try
                let length = api.Audio_LastError.Invoke(pointer, 2048)
                if length > 0 then Marshal.PtrToStringUTF8(pointer, length) else String.Empty
            finally Marshal.FreeHGlobal(pointer)
        | _ -> String.Empty
    static member private Scope(kind: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_GetScope) -> AudioScope(api.Audio_GetScope.Invoke(kind))
        | _ -> AudioScope(0UL)
    static member World = Audio.Scope(0)
    static member Session = Audio.Scope(1)
    static member SetMasterVolume(gain: single) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_SetBusVolume) -> api.Audio_SetBusVolume.Invoke(5, gain)
        | _ -> ()
    static member SetBusVolume(bus: AudioBus, gain: single) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_SetBusVolume) -> api.Audio_SetBusVolume.Invoke(int bus, gain)
        | _ -> ()
    static member private Play(scope: AudioScope, source: AudioSource, mode: int, position: Float3, owner: ObjectHandle,
                               settings: Nullable<AudioPlaySettings>, parameters: IReadOnlyList<AudioParameter>) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_Play) && source.Asset.IsValid ->
            let count = if isNull parameters then 0 else parameters.Count
            if count > 64 then invalidArg "parameters" "At most 64 audio parameters are supported."
            let allocations = ResizeArray<nativeint>()
            let values = if count = 0 then 0n else Marshal.AllocHGlobal(count * Marshal.SizeOf<ce_script_binding_AudioParameter>())
            try
                for index = 0 to count - 1 do
                    let parameter = parameters[index]
                    if String.IsNullOrEmpty(parameter.Name) || parameter.Name.Contains('\000') || Encoding.UTF8.GetByteCount(parameter.Name) > 256 ||
                       (not (isNull parameter.Text) && (parameter.Text.Contains('\000') || Encoding.UTF8.GetByteCount(parameter.Text) > 4096)) then
                        invalidArg "parameters" "Invalid audio parameter UTF-8 length."
                    let name = Marshal.StringToCoTaskMemUTF8(parameter.Name)
                    allocations.Add(name)
                    let text = if isNull parameter.Text then 0n else Marshal.StringToCoTaskMemUTF8(parameter.Text)
                    if text <> 0n then allocations.Add(text)
                    let mutable value = Unchecked.defaultof<ce_script_binding_AudioParameter>
                    value.name <- name; value.``type`` <- int parameter.Type; value.integer <- parameter.Integer
                    value.number <- parameter.Number; value.text <- text
                    Marshal.StructureToPtr(value, values + nativeint (index * Marshal.SizeOf<ce_script_binding_AudioParameter>()), false)
                let settingsPointer = NativePtr.stackalloc<ce_script_binding_AudioPlaySettings> 1
                if settings.HasValue then NativePtr.write settingsPointer (AudioSettingsConversion.toNative settings.Value)
                let settingsAddress = if settings.HasValue then NativePtr.toNativeInt settingsPointer else 0n
                PlaybackHandle(api.Audio_Play.Invoke(scope.Value, source.Asset.ToNative(), int source.Kind, mode, position.ToNative(), owner.ToNative(), settingsAddress, values, count))
            finally
                for pointer in allocations do Marshal.FreeCoTaskMem(pointer)
                Marshal.FreeHGlobal(values)
        | _ -> PlaybackHandle(0UL)
    static member Play2D(scope: AudioScope, source: AudioSource) = Audio.Play(scope, source, 0, Float3.Zero, ObjectHandle.Invalid, Nullable(), null)
    static member Play2D(scope: AudioScope, source: AudioSource, settings: AudioPlaySettings) = Audio.Play(scope, source, 0, Float3.Zero, ObjectHandle.Invalid, Nullable(settings), null)
    static member Play2D(scope: AudioScope, source: AudioSource, settings: Nullable<AudioPlaySettings>, parameters: IReadOnlyList<AudioParameter>) = Audio.Play(scope, source, 0, Float3.Zero, ObjectHandle.Invalid, settings, parameters)
    static member PlayAt(scope: AudioScope, source: AudioSource, position: Float3) = Audio.Play(scope, source, 1, position, ObjectHandle.Invalid, Nullable(), null)
    static member PlayAt(scope: AudioScope, source: AudioSource, position: Float3, settings: AudioPlaySettings) = Audio.Play(scope, source, 1, position, ObjectHandle.Invalid, Nullable(settings), null)
    static member PlayAt(scope: AudioScope, source: AudioSource, position: Float3, settings: Nullable<AudioPlaySettings>, parameters: IReadOnlyList<AudioParameter>) = Audio.Play(scope, source, 1, position, ObjectHandle.Invalid, settings, parameters)
    static member PlayAttached(scope: AudioScope, source: AudioSource, owner: Entity) = Audio.Play(scope, source, 2, Float3.Zero, owner.Handle, Nullable(), null)
    static member PlayAttached(scope: AudioScope, source: AudioSource, owner: Entity, settings: AudioPlaySettings) = Audio.Play(scope, source, 2, Float3.Zero, owner.Handle, Nullable(settings), null)
    static member PlayAttached(scope: AudioScope, source: AudioSource, owner: Entity, settings: Nullable<AudioPlaySettings>, parameters: IReadOnlyList<AudioParameter>) = Audio.Play(scope, source, 2, Float3.Zero, owner.Handle, settings, parameters)
    static member TryDequeueCompletion(output: byref<AudioPlaybackCompletion>) =
        output <- Unchecked.defaultof<AudioPlaybackCompletion>
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Audio_TakeCompletion) ->
            let pointer = NativePtr.stackalloc<ce_script_binding_AudioPlaybackCompletion> 1
            if api.Audio_TakeCompletion.Invoke(NativePtr.toNativeInt pointer) <> 0 then
                let value = NativePtr.read pointer
                output.PlaybackValue <- value.playback
                output.ScopeValue <- value.scope
                output.OwnerId <- value.ownerId
                output.Reason <- enum<AudioPlaybackEndReason>(value.reason)
                true
            else false
        | _ -> false
