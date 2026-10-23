namespace CreatorEngine.Scripts

open System
open System.Threading.Tasks
open CreatorEngine

type CharacterMovementProbe() =
    inherit Component()

    let mutable checkedOnce = false
    static let mutable captured = Unchecked.defaultof<CharacterMovementComponent>
    static let mutable completed = 0
    static let mutable passed = 0
    static let mutable failed = 0
    static let mutable additions = 0
    static let mutable removals = 0

    [<EngineCallable>]
    static member Lifecycle() =
        $"{{\"added\":{additions},\"removed\":{removals}}}"

    [<EngineCallable>]
    static member Results() =
        $"{{\"completed\":{completed},\"passed\":{passed},\"failed\":{failed}}}"

    member _.OnAddedToScene() : unit = additions <- additions + 1
    member _.OnRemovingFromScene() : unit = removals <- removals + 1

    [<EngineCallable>]
    static member Retired() =
        let mutable state = Unchecked.defaultof<CharacterMovementState>
        let zero = Unchecked.defaultof<Float3>

        let stale =
            not (obj.ReferenceEquals(captured, null))
            && captured.ReadState(&state) = PhysicsError.StaleHandle
            && state.Tick = 0UL
            && captured.SetDesiredVelocity(zero) = PhysicsError.StaleHandle
            && captured.Teleport(zero) = PhysicsError.StaleHandle
            && captured.Jump() = PhysicsError.StaleHandle
            && captured.ForceVelocity(zero, 1.0) = PhysicsError.StaleHandle
            && captured.CancelForcedVelocity() = PhysicsError.StaleHandle

        if stale then "retired" else "failure"

    [<EngineCallable>]
    static member Transferred() =
        let mutable state = Unchecked.defaultof<CharacterMovementState>

        let alive =
            not (obj.ReferenceEquals(captured, null))
            && captured.ReadState(&state) = PhysicsError.None
            && state.Simulating
            && state.DesiredVelocity.X = 1.25f
            && state.Forced
            && state.ForcedRemaining > 0.0

        if alive then "transferred" else "failure"

    [<EngineCallable>]
    static member JumpGrounded() =
        if obj.ReferenceEquals(captured, null) then
            "missing"
        else
            captured.Jump().ToString()

    member private _.Check(name, valid) =
        if valid then
            passed <- passed + 1
            Component.Log($"[CharacterMovementProbe] PASS {name}")
        else
            failed <- failed + 1
            Component.LogError($"[CharacterMovementProbe] FAIL {name}")

    member this.PostPhysics(_: single) : unit =
        if not checkedOnce then
            let character = this.GetComponent<CharacterMovementComponent>()

            if obj.ReferenceEquals(character, null) then
                checkedOnce <- true
                this.Check("lookup", false)
                completed <- completed + 1
            else
                let mutable state = Unchecked.defaultof<CharacterMovementState>

                if character.ReadState(&state) <> PhysicsError.None || state.Tick <> 0UL then
                    checkedOnce <- true
                    captured <- character
                    this.Check("lookup", true)

                    this.Check(
                        "state",
                        character.ReadState(&state) = PhysicsError.None
                        && state.Simulating
                        && state.Tick > 0UL
                    )

                    let same = CharacterMovementComponent.Find(this.Entity, character.ComponentId)

                    this.Check(
                        "identity",
                        not (obj.ReferenceEquals(same, null))
                        && same.ComponentId = character.ComponentId
                    )

                    this.Check(
                        "wrong identity",
                        obj.ReferenceEquals(CharacterMovementComponent.Find(this.Entity, UInt64.MaxValue), null)
                    )

                    this.Check("velocity", character.SetDesiredVelocity(Float3(1.0f, 0.0f, 0.0f)) = PhysicsError.None)

                    this.Check(
                        "input round trip",
                        character.ReadState(&state) = PhysicsError.None
                        && state.DesiredVelocity.X = 1.0f
                    )

                    this.Check(
                        "NaN rejected",
                        character.SetDesiredVelocity(Float3(Single.NaN, 0.0f, 0.0f)) = PhysicsError.InvalidArgument
                    )

                    this.Check(
                        "invalid input unchanged",
                        character.ReadState(&state) = PhysicsError.None
                        && state.DesiredVelocity.X = 1.0f
                    )

                    this.Check("teleport", character.Teleport(Float3(0.0f, 3.0f, 0.0f)) = PhysicsError.None)

                    this.Check(
                        "teleport state",
                        character.ReadState(&state) = PhysicsError.None
                        && state.Position.Y = 3.0f
                        && state.FallVelocity = 0.0f
                        && not state.Below
                        && not state.Above
                        && not state.Sides
                        && state.DesiredVelocity.X = 1.0f
                    )

                    this.Check("airborne jump rejected", character.Jump() = PhysicsError.WrongPhase)

                    this.Check(
                        "forced velocity",
                        character.ForceVelocity(Float3(2.0f, 0.0f, 0.0f), 0.5) = PhysicsError.None
                    )

                    this.Check(
                        "force state",
                        character.ReadState(&state) = PhysicsError.None
                        && state.Forced
                        && state.ForcedRemaining = 0.5
                    )

                    this.Check(
                        "invalid force",
                        character.ForceVelocity(Unchecked.defaultof<Float3>, -1.0) = PhysicsError.InvalidArgument
                    )

                    this.Check("cancel force", character.CancelForcedVelocity() = PhysicsError.None)

                    this.Check(
                        "cancel state",
                        character.ReadState(&state) = PhysicsError.None
                        && not state.Forced
                        && state.ForcedRemaining = 0.0
                    )

                    let foreign =
                        Task
                            .Run(
                                Func<PhysicsError>(fun () ->
                                    let mutable value = Unchecked.defaultof<CharacterMovementState>
                                    character.ReadState(&value))
                            )
                            .GetAwaiter()
                            .GetResult()

                    this.Check("foreign thread", (foreign = PhysicsError.WrongPhase))

                    try
                        (character :> Component).Enabled <- false
                        this.Check("activation boundary", false)
                    with :? NotSupportedException ->
                        this.Check("activation boundary", true)

                    completed <- completed + 1
