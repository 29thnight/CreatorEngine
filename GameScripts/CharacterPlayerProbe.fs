namespace CreatorEngine.Scripts

open System
open CreatorEngine

/// Observes completed physics in PostPhysics, preserving the frame/fixed boundary.
type CharacterPlayerProbe() =
    inherit Component()

    let mutable character = Unchecked.defaultof<CharacterMovementComponent>
    let mutable stage = 0
    let mutable passed = 0
    let mutable additions = 0
    let mutable removals = 0
    let mutable lastTick = 0UL
    let mutable forceStart = 0.0f
    let mutable movementStart = 0.0f
    let mutable finished = false
    let mutable transferred = false
    let mutable ddolFinished = false
    static let mutable sourceCharacter = Unchecked.defaultof<CharacterMovementComponent>
    static let mutable sourceBody = Unchecked.defaultof<PhysicsBodyComponent>
    static let mutable sourceActor = Unchecked.defaultof<Entity>
    static let mutable sourceFloor = Unchecked.defaultof<Entity>
    static let mutable sourceShape = 0u

    member private this.Check(valid, label) =
        if not valid then
            invalidOp label

        passed <- passed + 1

    member private this.ProbeLabel =
        match this.Entity.Name with
        | "ConvexGateActor" -> "physics.player.convex"
        | "HeightfieldGateActor" -> "physics.player.heightfield"
        | _ -> "physics.player"

    member _.VerifyFailedActivationWrappers() =
        if Environment.GetEnvironmentVariable("CE_PHYSICS_WRAPPER_PROBE") = "1" then
            let mutable checks = 0

            let require valid label =
                if not valid then
                    invalidOp label

                checks <- checks + 1

            let retained =
                Environment.GetEnvironmentVariable("CE_PHYSICS_WRAPPER_RETAINED") = "1"

            try
                require
                    (not (obj.ReferenceEquals(sourceCharacter, null))
                     && not (obj.ReferenceEquals(sourceBody, null)))
                    "source capture"

                require (not sourceFloor.IsAlive) "source floor retired"
                let mutable body = Unchecked.defaultof<PhysicsBodyState>
                require (sourceBody.ReadState(&body) = PhysicsError.StaleHandle && body.Mass = 0.0f) "body read"
                require (sourceBody.SetVelocity(Float3(9.0f, 0.0f, 0.0f)) = PhysicsError.StaleHandle) "body velocity"
                require (sourceBody.ApplyForce(Float3(9.0f, 0.0f, 0.0f)) = PhysicsError.StaleHandle) "body force"
                let mutable count = 0
                require (sourceBody.GetShapeCount(&count) = PhysicsError.StaleHandle && count = 0) "shape count"
                let mutable shape = Unchecked.defaultof<PhysicsShapeState>
                require (sourceBody.GetShape(0, &shape) = PhysicsError.StaleHandle && shape.ShapeId = 0u) "shape read"
                require (sourceBody.SetShapeFlags(sourceShape, true, false) = PhysicsError.StaleHandle) "shape flags"
                require (sourceActor.IsAlive = retained) "source actor lifetime"

                let read =
                    if retained then
                        PhysicsError.None
                    else
                        PhysicsError.StaleHandle

                let command =
                    if retained then
                        PhysicsError.WrongPhase
                    else
                        PhysicsError.StaleHandle

                let mutable before = Unchecked.defaultof<CharacterMovementState>

                require
                    (sourceCharacter.ReadState(&before) = read
                     && not before.Simulating
                     && (retained || before.Tick = 0UL))
                    "character read"

                require (sourceCharacter.SetDesiredVelocity(Float3(9.0f, 0.0f, 0.0f)) = command) "character velocity"
                require (sourceCharacter.Jump() = command) "character jump"
                require (sourceCharacter.ForceVelocity(Float3(9.0f, 0.0f, 0.0f), 1.0) = command) "character force"
                require (sourceCharacter.CancelForcedVelocity() = command) "character cancel"
                require (sourceCharacter.Teleport(Float3(99.0f, 99.0f, 99.0f)) = command) "character teleport"
                let mutable after = Unchecked.defaultof<CharacterMovementState>

                require
                    (sourceCharacter.ReadState(&after) = read
                     && after.Tick = before.Tick
                     && after.Position.X = before.Position.X
                     && after.Position.Y = before.Position.Y
                     && after.Position.Z = before.Position.Z
                     && after.DesiredVelocity.X = before.DesiredVelocity.X
                     && after.ForcedRemaining = before.ForcedRemaining
                     && after.Flags = before.Flags)
                    "rejected commands preserve state"

                let kept = retained.ToString().ToLowerInvariant()

                Console.WriteLine(
                    $"[physics.player.wrappers] {{\"passed\":{checks},\"failed\":0,\"retained\":{kept},\"complete\":true}}"
                )
            with error ->
                Console.WriteLine($"[physics.player.wrappers] {{\"passed\":{checks},\"failed\":1,\"complete\":false}}")
                Console.WriteLine($"[physics.player.wrappers.failure] {error.Message}")

    member _.OnAddedToScene() : unit =
        additions <- additions + 1

        if not (obj.ReferenceEquals(character, null)) then
            transferred <- true
            lastTick <- 0UL

    member _.OnRemovingFromScene() : unit = removals <- removals + 1

    member private this.VerifyTransfer() =
        if not ddolFinished then
            try
                let mutable state = Unchecked.defaultof<CharacterMovementState>

                if
                    obj.ReferenceEquals(character, null)
                    || character.ReadState(&state) <> PhysicsError.None
                then
                    invalidOp "retained wrapper read"

                if state.Tick >= 30UL then
                    Console.WriteLine(
                        FormattableString.Invariant(
                            $"[physics.player.ddol.state] positionY={state.Position.Y} footY={state.FootPosition.Y} below={state.Below} fall={state.FallVelocity} tick={state.Tick}"
                        )
                    )

                    passed <- 0
                    this.Check(additions = 2 && removals = 1, "balanced transfer lifecycle")
                    this.Check(state.Simulating, "destination simulation")
                    this.Check(state.DesiredVelocity.X = 1.25f, "retained desired input")

                    this.Check(
                        state.Forced && state.ForcedRemaining > 9.0 && state.ForcedRemaining <= 9.51,
                        "retained force duration"
                    )

                    this.Check(state.Position.X > 1.4f && state.Position.X < 1.8f, "continued forced movement")

                    let hierarchy =
                        Environment.GetEnvironmentVariable("CE_PHYSICS_DDOL_HIERARCHY") = "1"

                    this.Check(
                        (if hierarchy then
                             state.Position.Y < 4.99f && state.FootPosition.Y > 0.0f
                         else
                             state.Position.Y > 3.3f && state.Position.Y < 4.2f),
                        "continued gravity"
                    )

                    this.Check(state.FallVelocity < 0.0f, "fall state")
                    this.Check(state.Tick >= 30UL, "destination fixed ticks")
                    ddolFinished <- true

                    Console.WriteLine(
                        $"[physics.player.ddol] {{\"passed\":{passed},\"failed\":0,\"tick\":{state.Tick},\"complete\":true}}"
                    )
            with error ->
                ddolFinished <- true
                Console.WriteLine($"[physics.player.ddol] {{\"passed\":{passed},\"failed\":1,\"complete\":false}}")
                Console.WriteLine($"[physics.player.ddol.failure] {error.Message}")

    member this.PostPhysics(_: single) : unit =
        if transferred then
            this.VerifyTransfer()
        elif not finished then
            try
                if obj.ReferenceEquals(character, null) then
                    character <- this.GetComponent<CharacterMovementComponent>()

                if obj.ReferenceEquals(character, null) then
                    invalidOp "character lookup"

                let mutable state = Unchecked.defaultof<CharacterMovementState>

                if character.ReadState(&state) <> PhysicsError.None || not state.Simulating then
                    invalidOp "active state"

                if state.Tick <> 0UL && state.Tick <> lastTick then
                    lastTick <- state.Tick

                    if
                        obj.ReferenceEquals(sourceCharacter, null)
                        && this.Entity.Name = "CharacterGateActor"
                        && Environment.GetEnvironmentVariable("CE_PHYSICS_WRAPPER_PROBE") = "1"
                    then
                        sourceActor <- this.Entity
                        sourceFloor <- Entity.Find("CharacterGateFloor")
                        sourceBody <- sourceFloor.GetComponent<PhysicsBodyComponent>()
                        let mutable shape = Unchecked.defaultof<PhysicsShapeState>

                        if
                            obj.ReferenceEquals(sourceBody, null)
                            || sourceBody.GetShape(0, &shape) <> PhysicsError.None
                        then
                            invalidOp "source body capture"

                        sourceShape <- shape.ShapeId
                        sourceCharacter <- character

                    match stage with
                    | 0 when state.Below ->
                        this.Check(state.Tick > 0UL, "completed fixed step")
                        this.Check(abs state.FootPosition.Y < 0.08f, "cooked floor collision")

                        this.Check(
                            character.SetDesiredVelocity(Float3(1.0f, 0.0f, 0.0f)) = PhysicsError.None,
                            "velocity"
                        )

                        movementStart <- state.Position.X
                        stage <- 1
                    | 1 when state.Position.X - movementStart > 0.4f ->
                        this.Check(state.Below && state.MovementVelocity.X > 0.9f, "accelerated grounded movement")
                        this.Check(character.Jump() = PhysicsError.None, "jump")
                        stage <- 2
                    | 2 when state.FootPosition.Y > 0.3f ->
                        this.Check(not state.Below && state.FallVelocity > 0.0f, "jump rise")
                        this.Check(character.Jump() = PhysicsError.WrongPhase, "airborne jump rejection")
                        stage <- 3
                    | 3 when state.Below ->
                        this.Check(abs state.FootPosition.Y < 0.08f, "landing")
                        forceStart <- state.Position.X
                        this.Check(character.ForceVelocity(Float3(3.0f, 0.0f, 0.0f), 0.15) = PhysicsError.None, "force")
                        stage <- 4
                    | 4 when not state.Forced ->
                        this.Check(
                            state.ForcedRemaining = 0.0 && state.Position.X - forceStart >= 0.4f,
                            "force expiration"
                        )

                        this.Check(character.Teleport(Float3(0.0f, 5.0f, 0.0f)) = PhysicsError.None, "teleport")
                        let mutable moved = Unchecked.defaultof<CharacterMovementState>

                        this.Check(
                            character.ReadState(&moved) = PhysicsError.None
                            && moved.Position.Y = 5.0f
                            && moved.FallVelocity = 0.0f
                            && not moved.Below
                            && moved.MovementVelocity.X = 0.0f,
                            "teleport reset"
                        )

                        finished <- true

                        Console.WriteLine(
                            $"[{this.ProbeLabel}] {{\"passed\":{passed},\"failed\":0,\"tick\":{state.Tick},\"complete\":true}}"
                        )
                    | _ -> ()
            with error ->
                finished <- true
                Console.WriteLine($"[{this.ProbeLabel}] {{\"passed\":{passed},\"failed\":1,\"complete\":false}}")
                Console.WriteLine($"[physics.player.failure] {error.Message}")
