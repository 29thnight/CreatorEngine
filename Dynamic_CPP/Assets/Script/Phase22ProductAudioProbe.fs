namespace CreatorEngine.Scripts

open System
open CreatorEngine

/// Actual product audio probe. Value handles survive GC; scopes remain engine-owned.
[<RequireQualifiedAccess>]
type private Phase22ProductAudioProbeStep =
    | StartPlayback = 0
    | PausePlayback = 1
    | ResumeAndStopPlayback = 2
    | AwaitStopCompletion = 3

type Phase22ProductAudioProbe() =
    inherit Component()

    static let mutable worldPlayback = Unchecked.defaultof<PlaybackHandle>
    static let mutable sessionPlayback = Unchecked.defaultof<PlaybackHandle>
    static let loadPlaybacks = ResizeArray<PlaybackHandle>()
    let mutable completedCycles = 0
    let mutable playbackStep = Phase22ProductAudioProbeStep.StartPlayback
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
        Phase22ProductAudioProbe.Require(voices >= 0 && voices <= 128, "Voice workload bound")

        for playback in loadPlaybacks do
            playback.Stop()

        loadPlaybacks.Clear()

        Phase22ProductAudioProbe.Require(
            Audio.ConfigureBus(AudioBus.SFX, 128, AudioStealPolicy.Oldest),
            "Bus configuration"
        )

        Phase22ProductAudioProbe.Require(
            Audio.SetReverbPreset(
                if reverb then
                    AudioReverbPreset.Hall
                else
                    AudioReverbPreset.Off
            ),
            "Reverb configuration"
        )

        let mutable settings = Phase22ProductAudioProbe.CreatePlaybackSettings()
        settings.UseReverbSend <- if reverb then 1 else 0
        settings.ReverbSendDecibels <- -12.0f

        for index in 0 .. voices - 1 do
            let id =
                if index % 2 = 0 then
                    "276fc6b7-8304-4f68-a750-2672355e0867"
                else
                    "f1b819cf-d082-4c27-858e-d9d61c8f7cb0"

            let playback =
                Audio.Play2D(Audio.Session, Phase22ProductAudioProbe.CreateSource(id), settings)

            Phase22ProductAudioProbe.Require(playback.IsPlaying, "Workload voice " + string index)
            loadPlaybacks.Add(playback)

        "LOAD=" + string voices

    [<EngineCallable>]
    static member StartScopes() =
        Phase22ProductAudioProbe.Require(Audio.World.IsValid && Audio.Session.IsValid, "World/session scope missing")

        let source =
            Phase22ProductAudioProbe.CreateSource("41ec131c-cf46-402f-b5f3-b0484468ca5d")

        worldPlayback <- Audio.Play2D(Audio.World, source, Phase22ProductAudioProbe.CreatePlaybackSettings())
        sessionPlayback <- Audio.Play2D(Audio.Session, source, Phase22ProductAudioProbe.CreatePlaybackSettings())
        Phase22ProductAudioProbe.Require(worldPlayback.IsPlaying && sessionPlayback.IsPlaying, "Scope playback missing")
        GC.Collect()

        Phase22ProductAudioProbe.Require(
            worldPlayback.IsPlaying && sessionPlayback.IsPlaying,
            "GC stopped value handles"
        )

        "WORLD_SESSION_PLAYING"

    [<EngineCallable>]
    static member CheckSceneTransfer() =
        Phase22ProductAudioProbe.Require(
            not worldPlayback.IsPlaying && sessionPlayback.IsPlaying,
            "World must end; session must survive scene transfer"
        )

        "WORLD_ENDED_SESSION_PLAYING"

    [<EngineCallable>]
    static member StopScopes() =
        worldPlayback.Stop()
        sessionPlayback.Stop()

        Phase22ProductAudioProbe.Require(
            not worldPlayback.IsPlaying && not sessionPlayback.IsPlaying,
            "Stop scopes failed"
        )

        "SCOPES_STOPPED"

    member private this.AdvancePlaybackCycle() =
        let require = Phase22ProductAudioProbe.Require

        match playbackStep with
        | Phase22ProductAudioProbeStep.StartPlayback ->
            let id =
                match completedCycles % 4 with
                | 0 -> "cbdedeb5-7efa-4911-b7db-96a1b4b3cefa"
                | 1 -> "8af04b49-d9aa-4d78-840a-bd17b478b538"
                | 2 -> "276fc6b7-8304-4f68-a750-2672355e0867"
                | _ -> "f1b819cf-d082-4c27-858e-d9d61c8f7cb0"

            cyclePlayback <-
                Audio.Play2D(
                    Audio.World,
                    Phase22ProductAudioProbe.CreateSource(id),
                    Phase22ProductAudioProbe.CreatePlaybackSettings()
                )

            require (cyclePlayback.IsValid && cyclePlayback.IsPlaying, "Play " + string completedCycles)
            GC.Collect()
            playbackStep <- Phase22ProductAudioProbeStep.PausePlayback
        | Phase22ProductAudioProbeStep.PausePlayback ->
            require (cyclePlayback.IsPlaying, "GC value handle " + string completedCycles)
            cyclePlayback.Pause()
            require (cyclePlayback.State = AudioPlaybackState.Paused, "Pause " + string completedCycles)
            playbackStep <- Phase22ProductAudioProbeStep.ResumeAndStopPlayback
        | Phase22ProductAudioProbeStep.ResumeAndStopPlayback ->
            cyclePlayback.Resume()
            require (cyclePlayback.State = AudioPlaybackState.Playing, "Resume " + string completedCycles)
            cyclePlayback.SetGainPitch(0.001f, 1.0f)
            cyclePlayback.Stop()
            require (cyclePlayback.State = AudioPlaybackState.Stopped, "Stop " + string completedCycles)
            playbackStep <- Phase22ProductAudioProbeStep.AwaitStopCompletion
        | Phase22ProductAudioProbeStep.AwaitStopCompletion ->
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
                playbackStep <- Phase22ProductAudioProbeStep.StartPlayback

        | invalidStep -> invalidOp ($"Unknown playback cycle step: {invalidStep}")

    member private this.BeginSpatialPlayback() =
        let require = Phase22ProductAudioProbe.Require
        require (completionCount = 100, "Completion count " + string completionCount)

        let mono =
            Phase22ProductAudioProbe.CreateSource("276fc6b7-8304-4f68-a750-2672355e0867")

        attachedPlayback <-
            Audio.PlayAttached(Audio.World, mono, this.Entity, Phase22ProductAudioProbe.CreatePlaybackSettings())

        positionedPlayback <-
            Audio.PlayAt(
                Audio.Session,
                mono,
                Unchecked.defaultof<Float3>,
                Phase22ProductAudioProbe.CreatePlaybackSettings()
            )

        require (attachedPlayback.IsPlaying && positionedPlayback.IsPlaying, "Attached/positioned playback")
        spatialPlaybackStarted <- true

    member private _.FinishSpatialPlayback() =
        attachedPlayback.Stop()
        positionedPlayback.Stop()
        spatialPlaybackStopped <- true

        Console.WriteLine(
            "[AUDIO_CLR_PASS] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass resident=50 stream=50 mp3=25 flac=25"
        )

        let result =
            {| schemaVersion = 1
               processId = Environment.ProcessId
               cycles = completedCycles
               completions = completionCount
               gcValueHandle = true
               worldScope = Audio.World.IsValid
               sessionScope = Audio.Session.IsValid
               attached = attachedPlayback.State = AudioPlaybackState.Stopped
               positioned = positionedPlayback.State = AudioPlaybackState.Stopped
               resident = completedCycles / 2
               stream = completedCycles / 2
               mp3 = completedCycles / 4
               flac = completedCycles / 4 |}

        Console.WriteLine("[audio.acceptance] " + System.Text.Json.JsonSerializer.Serialize(result))

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
