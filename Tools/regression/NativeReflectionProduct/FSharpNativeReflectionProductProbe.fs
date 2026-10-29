namespace CreatorEngine.Scripts

open System
open System.Collections.Generic
open System.Text.Json
open CreatorEngine
open CreatorEngine.NativeTypes

/// Runs in the actual Editor and cooked Player, never in the portable provider fixture.
type FSharpNativeReflectionProductProbe() =
    inherit Component()

    static let mutable capturedDecal: NativeObject option = None
    let mutable character = Unchecked.defaultof<CharacterMovementComponent>
    let mutable sound: SoundComponentView option = None
    let mutable transform: TransformView option = None
    let mutable playback = PlaybackHandle(0UL)
    let mutable origin = 0.0f
    let mutable ticks = 0
    let mutable audioCompleted = false
    let mutable reported = false
    let mutable catalogComponents: string array = Array.empty
    let mutable catalogTypes = 0
    let checks = Dictionary<string, bool>(StringComparer.Ordinal)

    let check name condition =
        if not condition then
            invalidOp ("Native reflection product check failed: " + name)
        checks.[name] <- true

    let fail (error: exn) =
        Console.WriteLine("[native.reflection.failure] " + error.ToString())

    [<EngineCallable>]
    static member CheckRetiredDecal() =
        match capturedDecal with
        | None -> invalidOp "No native Decal instance was captured."
        | Some value ->
            try
                value.Describe() |> ignore
                false
            with
            | :? NativeReflectionException as error when error.Code = "stale_component" -> true

    member this.OnBeginSimulation() =
        try
            checks.Clear()
            ticks <- 0
            audioCompleted <- false
            reported <- false
            let catalog = NativeReflection.Types
            catalogTypes <- catalog.Length
            catalogComponents <-
                catalog
                |> Array.filter (fun item -> item.IsComponent && item.Name <> "Component")
                |> Array.map (fun item -> item.Name)
                |> Array.sort
            check "catalogUniqueIds" ((catalog |> Array.map _.Id |> Array.distinct |> Array.length) = catalog.Length)
            check "catalogScriptComponents"
                (Array.contains "ScriptComponent" catalogComponents && Array.contains "InvalidScriptComponent" catalogComponents)

            let nativeTransform = TransformView.From(this.Entity)
            transform <- Some nativeTransform
            origin <- nativeTransform.GetWorldPosition().x
            character <- this.GetComponent<CharacterMovementComponent>()
            check "characterExists" (not (isNull character))
            let nativeCharacter = CharacterMovementComponentView.From(this.Entity)
            let definition = nativeCharacter.CaptureDefinition()
            check "characterDefinition" definition.IsSuccess

            let light = LightComponentView.From(this.Entity)
            light.SetIntensity(2.75f)
            light.SetColor(math_colorView.Create(0.25f, 0.5f, 0.75f, 1.0f))
            check "lightScalarAndRecord" (light.m_intencity = 2.75f && light.m_color.r = 0.25f && light.m_color.b = 0.75f)

            let profile = SceneRenderProfileComponentView.From(this.Entity).GetRenderProfile()
            let before = profile.settings.bloom.threshold
            profile.settings.bloom.threshold <- 3.25f
            check "nestedProfileWrite" (profile.settings.bloom.threshold = 3.25f)
            profile.settings.bloom.threshold <- before

            let decal = DecalComponentView.From(this.Entity)
            decal.sliceX <- 4u
            decal.sliceY <- 5u
            decal.sliceNumber <- 6
            decal.slicePerSeconds <- 2.5f
            check "decalFields" (decal.sliceX = 4u && decal.sliceY = 5u && decal.sliceNumber = 6 && decal.slicePerSeconds = 2.5f)
            capturedDecal <- Some (this.Entity.GetNativeComponent("DecalComponent"))

            let machine = StateMachineComponentView.From(this.Entity)
            machine.name <- "reflection 한글 😀"
            check "unicodeField" (machine.name = "reflection 한글 😀")
            check "borrowedNullResult" (not (machine.FindStateByName("absent").HasValue))

            let nativeSound = SoundComponentView.From(this.Entity)
            sound <- Some nativeSound
            nativeSound.SetClipKey("ee03635c-f351-4d1e-b120-0a9f0848d4d4")
            nativeSound.SetVolume(0.001f)
            nativeSound.loop <- true
            nativeSound.spatial <- false
            let settings = nativeSound.ReadSettings()
            check "soundValueResult" (settings.volume = 0.001f && settings.loop && not settings.spatial)
            let result = nativeSound.PlayInstance(false)
            playback <- PlaybackHandle((uint64 result.generation <<< 32) ||| uint64 result.index)
            check "typedAudioPlayback" (playback.IsValid && playback.IsPlaying && nativeSound.IsPlaying())
            GC.Collect()
            check "audioSurvivesGc" playback.IsPlaying
        with error ->
            fail error
            reraise ()

    member this.OnTickSimulate(dt: single) =
        if not reported then
            try
                check "fixedStep" (abs (dt - 1.0f / 60.0f) < 1e-7f)
                let mutable status = Unchecked.defaultof<SimulationStatus>
                check "simulationOrder" (Simulation.GetStatus(&status) = SimulationResult.Ok && status.Executing <> 0UL)
                let mutable state = Unchecked.defaultof<CharacterMovementState>
                check "nativePhysicsRead" (character.ReadState(&state) = PhysicsError.None)
                check "nativePhysicsWrite" (character.SetDesiredVelocity(Float3(2.0f, 0.0f, 0.0f)) = PhysicsError.None)
                ticks <- ticks + 1
                let nativeSound = sound.Value
                if ticks = 2 then
                    nativeSound.Pause(true)
                    check "typedAudioPause" (playback.State = AudioPlaybackState.Paused)
                elif ticks = 3 then
                    nativeSound.Pause(false)
                    check "typedAudioResume" (playback.State = AudioPlaybackState.Playing)
                elif ticks = 4 then
                    nativeSound.Stop()
                    check "typedAudioStop" (playback.State = AudioPlaybackState.Stopped)

                let mutable completion = Unchecked.defaultof<AudioPlaybackCompletion>
                while Audio.TryDequeueCompletion(&completion) do
                    if completion.Playback = playback && completion.Reason = AudioPlaybackEndReason.Stopped then
                        audioCompleted <- true

                if ticks >= 30 && audioCompleted then
                    let moved = transform.Value.GetWorldPosition().x - origin
                    check "physicsMoved" (moved > 0.1f)
                    check "audioCompletion" audioCompleted
                    reported <- true
                    let result =
                        {| schemaVersion = 1
                           processId = Environment.ProcessId
                           protocol = "creator-native-reflection-v1"
                           nativeApi = NativeReflection.ApiVersion
                           catalogComponents = catalogComponents
                           catalogTypes = catalogTypes
                           fixedTicks = ticks
                           moved = moved
                           checks = checks |}
                    Console.WriteLine("[native.reflection.acceptance] " + JsonSerializer.Serialize(result))
                elif ticks >= 180 then
                    invalidOp "Native reflection product probe did not observe audio completion within 180 fixed ticks."
            with error ->
                fail error
                reraise ()

    member _.OnEndSimulation() =
        match sound with
        | Some value -> value.Stop()
        | None -> ()
