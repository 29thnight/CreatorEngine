namespace CreatorEngine.Scripts

open System
open CreatorEngine

type FSharpNativePlayerProbe() =
    inherit Component()

    let statusProfile = CreatorEngine.Diagnostics.ProfilerMarker("ScriptProbe.Status")
    let readProfile = CreatorEngine.Diagnostics.ProfilerMarker("ScriptProbe.Read")
    let velocityProfile = CreatorEngine.Diagnostics.ProfilerMarker("ScriptProbe.Velocity")
    let emptyProfile = CreatorEngine.Diagnostics.ProfilerMarker("ScriptProbe.Empty")
    let mutable character = Unchecked.defaultof<CharacterMovementComponent>
    let mutable transform = Unchecked.defaultof<Transform>
    let mutable count = 0
    let mutable pre = 0
    let mutable origin = 0.0f

    member _.OnInitialized() : unit =
        Console.WriteLine("[FSHARP_PLAYER_INIT]")

    member this.OnBeginSimulation() : unit =
        Console.WriteLine("[FSHARP_PLAYER_BEGIN]")
        transform <- this.GetComponent<Transform>()

        if obj.ReferenceEquals(transform, null) then
            failwith "F# Transform lookup failed"

        origin <- transform.WorldPosition.X
        Console.WriteLine("[FSHARP_PLAYER_BEGIN_READY]")

    member this.PrePhysics(dt: single) : unit =
        pre <- pre + 1

        if pre = 1 || pre = 60 then
            let mutable status = Unchecked.defaultof<SimulationStatus>
            let result = Simulation.GetStatus(&status)

            Console.WriteLine(
                $"[FSHARP_PLAYER_PRE] dt={dt} tick={status.Committed} enabled={this.Enabled} result={result}"
            )

    member this.OnTickSimulate(dt: single) : unit =
        try
            do
                use _scope = emptyProfile.Auto()
                ()
            if obj.ReferenceEquals(character, null) then
                character <- this.GetComponent<CharacterMovementComponent>()

            if obj.ReferenceEquals(character, null) then
                failwith "F# active CCT lookup failed"

            if abs (dt - 1.0f / 60.0f) > 1e-7f then
                failwith "F# fixed dt mismatch"

            let mutable status = Unchecked.defaultof<SimulationStatus>
            let mutable state = Unchecked.defaultof<CharacterMovementState>

            let statusResult =
                use _scope = statusProfile.Auto()
                Simulation.GetStatus(&status)

            if statusResult <> SimulationResult.Ok || status.Executing = 0UL then
                failwith "F# tick ordering"

            let readResult =
                use _scope = readProfile.Auto()
                character.ReadState(&state)

            if readResult <> PhysicsError.None then
                failwith "F# cached CCT read failed"

            let velocityResult =
                use _scope = velocityProfile.Auto()
                character.SetDesiredVelocity(Float3(2.0f, 0.0f, 0.0f))

            if velocityResult <> PhysicsError.None then
                failwith "F# CCT write failed"

            count <- count + 1

            if count = 30 then
                if transform.WorldPosition.X - origin < 0.1f then
                    failwith "F# native CCT command did not move"

                Console.WriteLine(
                    $"[FSHARP_PLAYER_OK] ticks={count} dt={dt} moved={transform.WorldPosition.X - origin} api={CreatorEngine.Native.reflgen_creator_script_native.ScriptApiVersion}"
                )
        with error ->
            Console.WriteLine("[FSHARP_PLAYER_FAIL] " + error.ToString())
            reraise ()
