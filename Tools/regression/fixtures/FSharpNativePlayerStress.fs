namespace CreatorEngine.Scripts

open System
open CreatorEngine

type FSharpNativePlayerProbe() =
    inherit Component()

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
            if obj.ReferenceEquals(character, null) then
                character <- this.GetComponent<CharacterMovementComponent>()

            if obj.ReferenceEquals(character, null) then
                failwith "F# active CCT lookup failed"

            if abs (dt - 1.0f / 60.0f) > 1e-7f then
                failwith "F# fixed dt mismatch"

            for _ in 1 .. 64 do
                let mutable status = Unchecked.defaultof<SimulationStatus>
                let mutable state = Unchecked.defaultof<CharacterMovementState>

                if Simulation.GetStatus(&status) <> SimulationResult.Ok || status.Executing = 0UL then
                    failwith "F# tick ordering"

                if character.ReadState(&state) <> PhysicsError.None then
                    failwith "F# cached CCT read failed"

                if character.SetDesiredVelocity(Float3(2.0f, 0.0f, 0.0f)) <> PhysicsError.None then
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
