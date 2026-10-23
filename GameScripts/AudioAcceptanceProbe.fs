namespace CreatorEngine.Scripts

open System
open CreatorEngine

/// Actual product audio probe. Value handles survive GC; scopes remain engine-owned.
[<RequireQualifiedAccess>]
type private AudioAcceptanceProbeStep =
    | StartPlayback = 0
    | PausePlayback = 1
    | ResumeAndStopPlayback = 2
    | AwaitStopCompletion = 3

type AudioAcceptanceProbe() =
    inherit Component()

    static let mutable worldPlayback = Unchecked.defaultof<PlaybackHandle>
    static let mutable sessionPlayback = Unchecked.defaultof<PlaybackHandle>
    static let loadPlaybacks = ResizeArray<PlaybackHandle>()
    let mutable completedCycles = 0
    let mutable playbackStep = AudioAcceptanceProbeStep.StartPlayback
    let mutable spatialPlaybackStarted = false
    let mutable spatialPlaybackStopped = false
    let mutable completionCount = 0
    let mutable cyclePlayback = Unchecked.defaultof<PlaybackHandle>
    let mutable attachedPlayback = Unchecked.defaultof<PlaybackHandle>
    let mutable positionedPlayback = Unchecked.defaultof<PlaybackHandle>
    static member private CreateSource(id) = AudioSource(AudioAssetId.Parse(id))

    static member private CreatePlaybackSettings() =
        let mutable settings = AudioPlaySettings()
        settings.Volume <- 0.001f
        settings.Loop <- 1
        settings.SpatialBlend <- 0.0f
        settings

    static member private Require(condition, message) =
        if not condition then
            invalidOp (message + " native=" + Audio.LastError)

    [<EngineCallable>]
    static member SetLoad(voices: int, reverb: bool) =
        AudioAcceptanceProbe.Require(voices >= 0 && voices <= 128, "Voice workload bound")

        for playback in loadPlaybacks do
            playback.Stop()

        loadPlaybacks.Clear()

        AudioAcceptanceProbe.Require(
            Audio.ConfigureBus(AudioBus.SFX, 128, AudioStealPolicy.Oldest),
            "Bus configuration"
        )

        AudioAcceptanceProbe.Require(
            Audio.SetReverbPreset(
                if reverb then
                    AudioReverbPreset.Hall
                else
                    AudioReverbPreset.Off
            ),
            "Reverb configuration"
        )

        let mutable settings = AudioAcceptanceProbe.CreatePlaybackSettings()
        settings.UseReverbSend <- if reverb then 1 else 0
        settings.ReverbSendDecibels <- -12.0f

        for index in 0 .. voices - 1 do
            let id =
                if index % 2 = 0 then
                    "41ec131c-cf46-402f-b5f3-b0484468ca5d"
                else
                    "f1b819cf-d082-4c27-858e-d9d61c8f7cb0"

            let playback =
                Audio.Play2D(Audio.Session, AudioAcceptanceProbe.CreateSource(id), settings)

            AudioAcceptanceProbe.Require(playback.IsPlaying, "Workload voice " + string index)
            loadPlaybacks.Add(playback)

        "LOAD=" + string voices

    [<EngineCallable>]
    static member StartScopes() =
        AudioAcceptanceProbe.Require(Audio.World.IsValid && Audio.Session.IsValid, "World/session scope missing")

        let source =
            AudioAcceptanceProbe.CreateSource("41ec131c-cf46-402f-b5f3-b0484468ca5d")

        worldPlayback <- Audio.Play2D(Audio.World, source, AudioAcceptanceProbe.CreatePlaybackSettings())
        sessionPlayback <- Audio.Play2D(Audio.Session, source, AudioAcceptanceProbe.CreatePlaybackSettings())
        AudioAcceptanceProbe.Require(worldPlayback.IsPlaying && sessionPlayback.IsPlaying, "Scope playback missing")
        GC.Collect()
        AudioAcceptanceProbe.Require(worldPlayback.IsPlaying && sessionPlayback.IsPlaying, "GC stopped value handles")
        "WORLD_SESSION_PLAYING"

    [<EngineCallable>]
    static member CheckSceneTransfer() =
        AudioAcceptanceProbe.Require(
            not worldPlayback.IsPlaying && sessionPlayback.IsPlaying,
            "World must end; session must survive scene transfer"
        )

        "WORLD_ENDED_SESSION_PLAYING"

    [<EngineCallable>]
    static member StopScopes() =
        worldPlayback.Stop()
        sessionPlayback.Stop()
        AudioAcceptanceProbe.Require(not worldPlayback.IsPlaying && not sessionPlayback.IsPlaying, "Stop scopes failed")
        "SCOPES_STOPPED"

    member private this.AdvancePlaybackCycle() =
        let require = AudioAcceptanceProbe.Require

        match playbackStep with
        | AudioAcceptanceProbeStep.StartPlayback ->
            let id = "41ec131c-cf46-402f-b5f3-b0484468ca5d"

            cyclePlayback <-
                Audio.Play2D(
                    Audio.World,
                    AudioAcceptanceProbe.CreateSource(id),
                    AudioAcceptanceProbe.CreatePlaybackSettings()
                )

            require (cyclePlayback.IsValid && cyclePlayback.IsPlaying, "Play " + string completedCycles)
            GC.Collect()
            playbackStep <- AudioAcceptanceProbeStep.PausePlayback
        | AudioAcceptanceProbeStep.PausePlayback ->
            require (cyclePlayback.IsPlaying, "GC value handle " + string completedCycles)
            cyclePlayback.Pause()
            require (cyclePlayback.State = AudioPlaybackState.Paused, "Pause " + string completedCycles)
            playbackStep <- AudioAcceptanceProbeStep.ResumeAndStopPlayback
        | AudioAcceptanceProbeStep.ResumeAndStopPlayback ->
            cyclePlayback.Resume()
            require (cyclePlayback.State = AudioPlaybackState.Playing, "Resume " + string completedCycles)
            cyclePlayback.SetGainPitch(0.001f, 1.0f)
            cyclePlayback.Stop()
            require (cyclePlayback.State = AudioPlaybackState.Stopped, "Stop " + string completedCycles)
            playbackStep <- AudioAcceptanceProbeStep.AwaitStopCompletion
        | AudioAcceptanceProbeStep.AwaitStopCompletion ->
            let mutable complete = false
            let mutable completion = Unchecked.defaultof<AudioPlaybackCompletion>

            while Audio.TryDequeueCompletion(&completion) do
                if
                    completion.Playback = cyclePlayback
                    && completion.Reason = AudioPlaybackEndReason.Stopped
                then
                    completionCount <- completionCount + 1
                    complete <- true

            if complete then
                completedCycles <- completedCycles + 1
                playbackStep <- AudioAcceptanceProbeStep.StartPlayback

        | invalidStep -> invalidOp ($"Unknown playback cycle step: {invalidStep}")

    member private this.BeginSpatialPlayback() =
        let require = AudioAcceptanceProbe.Require
        require (completionCount = 100, "Completion count " + string completionCount)
        let mono = AudioAcceptanceProbe.CreateSource("276fc6b7-8304-4f68-a750-2672355e0867")

        attachedPlayback <-
            Audio.PlayAttached(Audio.World, mono, this.Entity, AudioAcceptanceProbe.CreatePlaybackSettings())

        positionedPlayback <-
            Audio.PlayAt(
                Audio.Session,
                mono,
                Unchecked.defaultof<Float3>,
                AudioAcceptanceProbe.CreatePlaybackSettings()
            )

        require (attachedPlayback.IsPlaying && positionedPlayback.IsPlaying, "Attached/positioned playback")
        spatialPlaybackStarted <- true

    member private _.FinishSpatialPlayback() =
        attachedPlayback.Stop()
        positionedPlayback.Stop()
        spatialPlaybackStopped <- true

        Console.WriteLine(
            "[AUDIO_CLR_PASS] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass"
        )


    member this.OnTickSimulate(_: single) : unit =
        if completedCycles < 100 then
            this.AdvancePlaybackCycle()
        elif not spatialPlaybackStarted then
            this.BeginSpatialPlayback()
        elif not spatialPlaybackStopped then
            this.FinishSpatialPlayback()

    member _.OnEndSimulation() : unit =
        cyclePlayback.Stop()
        attachedPlayback.Stop()
        positionedPlayback.Stop()
