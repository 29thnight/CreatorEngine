namespace CreatorEngine

open System.Runtime.InteropServices

type AudioOwnerDestroyedPolicy = Stop = 0 | DetachAndFinish = 1
type AudioBus = BGM = 0 | SFX = 1 | Player = 2 | Monster = 3 | UI = 4
type AudioRolloff = Linear = 0 | Inverse = 1 | Custom = 2

[<Struct; StructLayout(LayoutKind.Sequential)>]
type AudioPlaySettings =
    val mutable Volume: single
    val mutable Pitch: single
    val mutable Priority: int
    val mutable Loop: int
    val mutable Bus: AudioBus
    val mutable SpatialBlend: single
    val mutable MinimumDistance: single
    val mutable MaximumDistance: single
    val mutable Rolloff: AudioRolloff
    val mutable UseReverbSend: int
    val mutable ReverbSendDecibels: single
    val mutable OwnerDestroyedPolicy: AudioOwnerDestroyedPolicy
    val mutable ConcurrencyGroup: uint32
    val mutable PreemptSameClip: int
    val mutable AllowVirtualization: int
    static member Default =
        let mutable value = Unchecked.defaultof<AudioPlaySettings>
        value.Volume <- 1.0f; value.Pitch <- 1.0f; value.Priority <- 128; value.Bus <- AudioBus.SFX
        value.SpatialBlend <- 1.0f; value.MinimumDistance <- 1.0f; value.MaximumDistance <- 50.0f
        value.Rolloff <- AudioRolloff.Inverse; value.AllowVirtualization <- 1
        value
