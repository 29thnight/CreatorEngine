namespace CreatorEngine.Scripts

open System
open CreatorEngine

/// 명령 접수 → 고정틱 실행 → 물리 완료 스냅샷 → 중지/재시작을 실제 호스트에서 검증한다.
type SimulationExchangeContractProbe() =
    inherit Component()

    // Play를 다시 시작해도 유지해서, 외부 회귀 도구가 두 실행을 구분한다.
    static let mutable completedRuns = 0
    static let mutable failureCount = 0
    static let mutable endedRuns = 0
    static let mutable activeEpoch = 0UL
    static let mutable activeDriver: SimulationExchangeContractProbe option = None

    // 이 인스턴스의 현재 Play 실행에만 속하는 상태다.
    let mutable completedThisRun = false
    let mutable hasPausedCommand = false
    let mutable pausedCommandTick = 0UL
    let mutable executedTicks = 0

    static member private Require(condition, reason) =
        if not condition then
            failureCount <- failureCount + 1
            invalidOp ("[N3_FAIL] " + reason)

    static member private ReadStatus() =
        let mutable status = Unchecked.defaultof<SimulationStatus>
        let result = Simulation.GetStatus(&status)

        SimulationExchangeContractProbe.Require((result = SimulationResult.Ok), "status")
        status

    static member private SubmitCommand(epoch, tick, kind, payload: byte[]) =
        let mutable sequence = 0UL
        Simulation.Submit(epoch, tick, kind, ReadOnlySpan<byte>(payload), &sequence)

    member private _.QueuePosition(tick, position, owner: Entity) =
        let mutable sequence = 0UL

        let result =
            Simulation.SetWorldPosition(activeEpoch, tick, owner, position, &sequence)

        SimulationExchangeContractProbe.Require((result = SimulationResult.Ok), "position admission")
        sequence

    member private this.QueueWhilePaused() =
        let status = SimulationExchangeContractProbe.ReadStatus()
        let require = SimulationExchangeContractProbe.Require

        require (status.Executing = 0UL, "pause status")

        pausedCommandTick <- status.NextTick
        hasPausedCommand <- true

        this.QueuePosition(status.NextTick, Float3(70.0f, 0.0f, 0.0f), this.Entity)
        |> ignore

    member private this.VerifyInitialCommandAdmission() =
        let require = SimulationExchangeContractProbe.Require
        let submitCommand = SimulationExchangeContractProbe.SubmitCommand

        // 같은 틱에 들어온 명령은 접수 순서로 적용되어 최종 X가 20이 된다.
        let firstSequence = this.QueuePosition(2UL, Float3(10.0f, 0.0f, 0.0f), this.Entity)
        let secondSequence = this.QueuePosition(2UL, Float3(20.0f, 0.0f, 0.0f), this.Entity)
        require (secondSequence > firstSequence, "FIFO admission")

        // 접수는 성공하지만 실행 시 거부할 세 가지 입력이다.
        let mutable invalidTargetSequence = 0UL

        let invalidTargetResult =
            Simulation.SetWorldPosition(
                activeEpoch,
                2UL,
                ObjectHandle.Invalid,
                Float3(99.0f, 0.0f, 0.0f),
                &invalidTargetSequence
            )

        require (invalidTargetResult = SimulationResult.Ok, "invalid target admission")
        require (submitCommand (activeEpoch, 2UL, 999u, [||]) = SimulationResult.Ok, "unknown command receipt")
        this.QueuePosition(2UL, Float3(Single.NaN, 0.0f, 0.0f), this.Entity) |> ignore

        // 접수 시점에 거부해야 하는 경계 값은 실행 단계 검사와 분리한다.
        let rejectedAdmissions =
            [
                activeEpoch - 1UL, 2UL, [||], SimulationResult.StaleEpoch
                activeEpoch, 0UL, [||], SimulationResult.Late
                activeEpoch, 129UL, [||], SimulationResult.TooFar
                activeEpoch, 3UL, Array.zeroCreate<byte> 65, SimulationResult.InvalidArgument
            ]

        for epoch, tick, payload, expectedResult in rejectedAdmissions do
            let actualResult = submitCommand (epoch, tick, 1u, payload)
            require (actualResult = expectedResult, "bounded admission")

    member private _.VerifyCommittedSnapshot() =
        let require = SimulationExchangeContractProbe.Require
        let mutable header = Unchecked.defaultof<SimulationSnapshot>

        // 작은 버퍼로 읽은 실패는 커서를 소모하지 않아야 한다.
        let discoveryResult =
            Simulation.ReadSnapshot(activeEpoch, 1UL, Span<byte>.Empty, &header)

        require (discoveryResult = SimulationResult.BufferTooSmall && header.Bytes = 24u, "small read retry")

        let payload = Array.zeroCreate<byte> 24

        let readResult =
            Simulation.ReadSnapshot(activeEpoch, 1UL, Span<byte>(payload), &header)

        let receiptMatches =
            readResult = SimulationResult.Ok
            && header.Tick = 2UL
            && header.Applied = 2u
            && header.Rejected = 3u
            && header.Schema = 1u

        require (receiptMatches, "committed receipt")

        let positionX = BitConverter.ToSingle(payload, 8)
        let sceneValue = BitConverter.ToUInt32(payload, 20)
        require (abs (positionX - 20.0f) < 0.001f && sceneValue = 1u, "post physics snapshot")

    member private _.VerifySnapshotRetention(status: SimulationStatus) =
        let payload = Array.zeroCreate<byte> 4096
        let mutable header = Unchecked.defaultof<SimulationSnapshot>
        let result = Simulation.ReadSnapshot(activeEpoch, 0UL, Span<byte>(payload), &header)

        let reportsOverwrittenTicks =
            result = SimulationResult.SnapshotGap && header.Tick = status.Committed - 7UL

        SimulationExchangeContractProbe.Require(reportsOverwrittenTicks, "bounded snapshot loss signal")

    [<EngineCallable>]
    static member State() =
        let status = SimulationExchangeContractProbe.ReadStatus()

        String.concat
            ""
            [
                $"{{\"completed\":{completedRuns},\"failures\":{failureCount},\"ended\":{endedRuns},"
                $"\"epoch\":{status.Epoch},\"committed\":{status.Committed},\"executing\":{status.Executing},"
                $"\"queued\":{status.Queued},\"active\":{status.Active}}}"
            ]

    [<EngineCallable>]
    static member QueuePaused() =
        match activeDriver with
        | Some driver ->
            driver.QueueWhilePaused()
            true
        | None ->
            SimulationExchangeContractProbe.Require(false, "driver missing")
            false

    [<EngineCallable>]
    static member CheckStopped() =
        let require = SimulationExchangeContractProbe.Require
        let status = SimulationExchangeContractProbe.ReadStatus()

        let oldEpochResult =
            SimulationExchangeContractProbe.SubmitCommand(activeEpoch, 1UL, 1u, [||])

        require (status.Active = 0u, "stopped inactive")
        require (oldEpochResult = SimulationResult.StaleEpoch, "stop invalidation")
        true

    member _.OnInitialized() : unit =
        Console.WriteLine("[SMOKE] managed OnInitialized: SimulationExchangeContractProbe")

    member this.OnBeginSimulation() : unit =
        Console.WriteLine("[SMOKE] managed OnBeginSimulation: SimulationExchangeContractProbe")
        activeDriver <- Some this

        let status = SimulationExchangeContractProbe.ReadStatus()
        SimulationExchangeContractProbe.Require(status.Active = 1u && status.Committed = 0UL, "new play epoch")

        activeEpoch <- status.Epoch
        this.VerifyInitialCommandAdmission()

    member this.OnTickSimulate(dt: single) : unit =
        executedTicks <- executedTicks + 1

        let require = SimulationExchangeContractProbe.Require
        let status = SimulationExchangeContractProbe.ReadStatus()

        let executionOrderMatches =
            status.Epoch = activeEpoch
            && status.Executing = uint64 executedTicks
            && status.Committed + 1UL = status.Executing

        require (executionOrderMatches, "execution/commit order")
        require (abs (dt - 1.0f / 60.0f) < 1e-7f, "fixed delta")

        let reentryResult =
            SimulationExchangeContractProbe.SubmitCommand(activeEpoch, status.Executing, 1u, [||])

        require (reentryResult = SimulationResult.Late, "no reentry")

        let positionX = this.Entity.Transform.WorldPosition.X

        match executedTicks with
        | 1 ->
            require (abs positionX < 0.001f, "future command ran early")
            this.QueuePosition(4UL, Float3(40.0f, 0.0f, 0.0f), this.Entity) |> ignore

            let mutable header = Unchecked.defaultof<SimulationSnapshot>
            let result = Simulation.ReadSnapshot(activeEpoch, 0UL, Span<byte>.Empty, &header)
            require (result = SimulationResult.NoSnapshot, "no uncommitted snapshot")
        | 2 -> require (abs (positionX - 20.0f) < 0.001f, "FIFO commands before update")
        | 3 -> this.VerifyCommittedSnapshot()
        | 4 -> require (abs (positionX - 40.0f) < 0.001f, "future batch position")
        | _ -> ()

        if hasPausedCommand && status.Executing = pausedCommandTick then
            require (abs (positionX - 70.0f) < 0.001f, "paused queue applied on resume")

        if executedTicks >= 14 && not completedThisRun then
            this.VerifySnapshotRetention(status)
            completedThisRun <- true
            completedRuns <- completedRuns + 1

            Console.WriteLine(
                $"[N3_PRODUCT_OK] completed={completedRuns} epoch={activeEpoch} tick={status.Executing} applied=FIFO snapshot=postphysics gap=explicit"
            )

    member _.OnEndSimulation() : unit =
        endedRuns <- endedRuns + 1
        activeDriver <- None
