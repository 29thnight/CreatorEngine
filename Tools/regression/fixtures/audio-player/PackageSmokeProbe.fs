namespace CreatorEngine.Scripts

open System
open CreatorEngine

type PackageSmokeProbe() =
    inherit Component()
    let mutable cycle = 0
    let mutable phase = 0
    let mutable completions = 0
    let mutable handle = Unchecked.defaultof<PlaybackHandle>
    let mutable attached = Unchecked.defaultof<PlaybackHandle>
    let mutable positioned = Unchecked.defaultof<PlaybackHandle>

    member private _.Require(condition, message) =
        if not condition then
            Console.WriteLine($"[AUDIO_CLR_FAIL] {message} native={Audio.LastError}")
            invalidOp message

    member _.OnInitialized() = Console.WriteLine("[SMOKE] managed OnInitialized: PackageSmokeProbe")

    member this.OnBeginSimulation() =
        Console.WriteLine("[SMOKE] managed OnBeginSimulation: PackageSmokeProbe")
        this.Require(Audio.World.IsValid && Audio.Session.IsValid, "World and session scopes")
        this.Require(Audio.ConfigureBus(AudioBus.SFX, 128, AudioStealPolicy.Oldest), "Configure bus")
        this.Require(Audio.SetReverbPreset(AudioReverbPreset.Hall), "Configure reverb")

    member this.OnTickSimulate(_: single) =
        let mutable settings = AudioPlaySettings.Default
        settings.Volume <- 0.001f
        settings.Loop <- 1
        settings.SpatialBlend <- 0.0f
        if cycle < 100 then
            if phase = 0 then
                handle <- Audio.Play2D(Audio.World, AudioSource(AudioAssetId.Parse("01234567-89ab-4cde-8fab-0123456789ab")), settings)
                this.Require(handle.IsValid && handle.IsPlaying, $"Play {cycle}")
                GC.Collect()
                phase <- 1
            elif phase = 1 then
                this.Require(handle.IsPlaying, $"GC value handle {cycle}")
                handle.Pause()
                this.Require(handle.State = AudioPlaybackState.Paused, $"Pause {cycle}")
                phase <- 2
            elif phase = 2 then
                handle.Resume()
                this.Require(handle.State = AudioPlaybackState.Playing, $"Resume {cycle}")
                handle.SetGainPitch(0.001f, 1.0f)
                handle.Stop()
                this.Require(handle.State = AudioPlaybackState.Stopped, $"Stop {cycle}")
                phase <- 3
            else
                let mutable completed = false
                let mutable completion = Unchecked.defaultof<AudioPlaybackCompletion>
                while Audio.TryDequeueCompletion(&completion) do
                    if completion.Playback = handle && completion.Reason = AudioPlaybackEndReason.Stopped then
                        completions <- completions + 1
                        completed <- true
                if completed then
                    cycle <- cycle + 1
                    phase <- 0
        elif phase = 0 then
            this.Require((completions = 100), $"Completion count {completions}")
            let mono = AudioSource(AudioAssetId.Parse("01234567-89ab-4cde-8fab-0123456789ab"))
            attached <- Audio.PlayAttached(Audio.World, mono, this.Entity, settings)
            positioned <- Audio.PlayAt(Audio.Session, mono, Float3.Zero, settings)
            this.Require(attached.IsPlaying && positioned.IsPlaying, "Attached/positioned playback")
            phase <- 1
        elif phase = 1 then
            attached.Stop()
            positioned.Stop()
            phase <- 2
            Console.WriteLine("[AUDIO_CLR_PASS] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass")

    member _.OnEndSimulation() =
        handle.Stop()
        attached.Stop()
        positioned.Stop()
