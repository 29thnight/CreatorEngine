namespace CreatorEngine.Scripts

open System
open CreatorEngine

type AudioEditorProbe() =
    inherit Component()
    static let mutable world = Unchecked.defaultof<PlaybackHandle>
    static let mutable session = Unchecked.defaultof<PlaybackHandle>
    static let mutable attached = Unchecked.defaultof<PlaybackHandle>
    static let mutable starts = 0
    static let mutable stops = 0

    static member private Require(value, message) =
        if not value then
            Console.WriteLine($"[AUDIO_EDITOR_FAIL] {message} native={Audio.LastError}")
            invalidOp message

    member this.OnBeginSimulation() =
        AudioEditorProbe.Require(world.State = AudioPlaybackState.Stopped && session.State = AudioPlaybackState.Stopped
                                 && attached.State = AudioPlaybackState.Stopped, "Previous generation cleaned")
        let source = AudioSource(AudioAssetId.Parse("01234567-89ab-4cde-8fab-0123456789ab"))
        let mutable settings = AudioPlaySettings.Default
        settings.Volume <- 0.001f
        settings.Loop <- 1
        settings.SpatialBlend <- 0.0f
        world <- Audio.Play2D(Audio.World, source, settings)
        session <- Audio.Play2D(Audio.Session, source, settings)
        attached <- Audio.PlayAttached(Audio.World, source, this.Entity, settings)
        AudioEditorProbe.Require(world.IsValid && session.IsValid && attached.IsValid, "Three native play paths")
        GC.Collect()
        AudioEditorProbe.Require(world.IsPlaying && session.IsPlaying && attached.IsPlaying, "GC preserves value handles")
        starts <- starts + 1
        Console.WriteLine($"[AUDIO_EDITOR_BEGIN] starts={starts}")

    [<EngineCallable>]
    static member AssertStopped() =
        AudioEditorProbe.Require(world.State = AudioPlaybackState.Stopped && session.State = AudioPlaybackState.Stopped
                                 && attached.State = AudioPlaybackState.Stopped, "Stop ends world/session/attached playback")
        stops <- stops + 1
        Console.WriteLine($"[AUDIO_EDITOR_STOP] stops={stops}")
