namespace CreatorEngine.Scripts

open System
open CreatorEngine

type CharacterPlanarInputProbe() =
    inherit Component()

    let mutable character = Unchecked.defaultof<CharacterMovementComponent>
    let mutable startTick = 0UL
    let mutable lastTick = 0UL
    let mutable start = Unchecked.defaultof<Float3>
    let mutable stage = 0
    let mutable passed = 0
    let mutable finished = false
    static let mutable retired = Unchecked.defaultof<CharacterMovementComponent>
    member _.OnEndSimulation() : unit = retired <- character

    [<EngineCallable>]
    static member Retired() =
        if obj.ReferenceEquals(retired, null) then
            "missing"
        else
            let mutable state = Unchecked.defaultof<CharacterMovementState>

            if
                retired.ReadState(&state) = PhysicsError.StaleHandle
                && state.Tick = 0UL
                && retired.SetPlanarInput(Float3.Right, 4.0f) = PhysicsError.StaleHandle
            then
                "retired"
            else
                "retargeted"

    member private _.Require(valid, label) =
        if not valid then
            invalidOp label

        passed <- passed + 1

    member this.OnBeginSimulation() : unit =
        character <- this.GetComponent<CharacterMovementComponent>()

        if obj.ReferenceEquals(character, null) then
            invalidOp "character binding"

        this.Transform.SetWorldRotation(Quaternion.LookRotation(Float3.Forward))
        this.Require(character.SetPlanarInput(Float3(0.25f, 7.0f, 0.0f), 4.0f) = PhysicsError.None, "analog request")

    member this.PostPhysics(_: single) : unit =
        if not finished then
            try
                let mutable state = Unchecked.defaultof<CharacterMovementState>

                if
                    obj.ReferenceEquals(character, null)
                    || character.ReadState(&state) <> PhysicsError.None
                    || not state.Simulating
                then
                    invalidOp "active character"

                if state.Tick <> 0UL && state.Tick <> lastTick then
                    lastTick <- state.Tick

                    if startTick = 0UL then
                        startTick <- state.Tick
                        start <- state.Position

                    if state.Tick - startTick >= 60UL then
                        match stage with
                        | 0 ->
                            this.Require(
                                state.DesiredVelocity.X = 1.0f && state.DesiredVelocity.Y = 0.0f,
                                "analog desired velocity"
                            )

                            this.Require(
                                state.Position.X > start.X + 0.7f && abs (state.Position.Z - start.Z) < 0.02f,
                                "analog physical movement"
                            )

                            this.Require(
                                Float3.Dot(this.Transform.Forward, Float3.Forward) > 0.999f,
                                "strafe preserves facing"
                            )

                            this.Require(
                                character.SetPlanarInput(Float3(1.0f, 0.0f, 1.0f), 4.0f) = PhysicsError.None,
                                "diagonal request"
                            )
                        | 1 ->
                            this.Require(abs (state.DesiredVelocity.Length - 4.0f) < 1e-4f, "diagonal speed cap")

                            this.Require(
                                state.Position.X > start.X + 1.0f && state.Position.Z > start.Z + 1.0f,
                                "diagonal physical movement"
                            )

                            this.Transform.SetWorldRotation(Quaternion.LookRotation(Float3.Right))

                            this.Require(
                                character.SetPlanarInput(Unchecked.defaultof<Float3>, 4.0f) = PhysicsError.None,
                                "release request"
                            )
                        | 2 ->
                            this.Require(
                                state.DesiredVelocity.LengthSquared = 0.0f
                                && state.MovementVelocity.Length < 0.01f,
                                "fixed step braking"
                            )

                            this.Require(
                                Float3.Dot(this.Transform.Forward, Float3.Right) > 0.999f,
                                "explicit script facing"
                            )

                            this.Require(
                                character.SetPlanarInput(Float3(Single.NaN, 0.0f, 0.0f), 4.0f) =
                                    PhysicsError.InvalidArgument,
                                "invalid input rejection"
                            )

                            this.Require(
                                character.ForceVelocity(Float3(0.0f, 0.0f, 2.0f), 2.0) = PhysicsError.None,
                                "independent forced movement"
                            )
                        | 3 ->
                            this.Require(
                                state.Forced && state.DesiredVelocity.LengthSquared = 0.0f,
                                "force preserves released input"
                            )

                            this.Require(state.Position.Z > start.Z + 1.8f, "forced physical movement")

                            this.Require(
                                Float3.Dot(this.Transform.Forward, Float3.Right) > 0.999f,
                                "force preserves explicit facing"
                            )

                            this.Require(character.CancelForcedVelocity() = PhysicsError.None, "force cleanup")
                            finished <- true

                            Console.WriteLine(
                                $"[physics.player.planar] {{\"passed\":{passed},\"failed\":0,\"tick\":{state.Tick},\"complete\":true}}"
                            )
                        | _ -> ()

                        stage <- stage + 1
                        startTick <- state.Tick
                        start <- state.Position
            with error ->
                finished <- true
                Console.WriteLine($"[physics.player.planar] {{\"passed\":{passed},\"failed\":1,\"complete\":false}}")
                Console.WriteLine($"[physics.player.planar.failure] {error.Message}")
