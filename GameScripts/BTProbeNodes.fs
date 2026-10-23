namespace GameScripts

open CreatorEngine

type BTProbeAlwaysTrue() =
    inherit ConditionNode()
    static member CountKey = "BTProbe.ConditionCount"

    override _.ConditionCheck(_, board) =
        board.SetInt(BTProbeAlwaysTrue.CountKey, board.GetInt(BTProbeAlwaysTrue.CountKey) + 1)
        true

type BTProbeAuthoredValue() =
    inherit ConditionNode()
    static member AuthoredKey = "BTProbe.Authored"
    static member SeenKey = "BTProbe.AuthoredSeen"
    static member Expected = 4242

    override _.ConditionCheck(_, board) =
        let matched =
            board.GetInt(BTProbeAuthoredValue.AuthoredKey) = BTProbeAuthoredValue.Expected

        if matched then
            board.SetInt(BTProbeAuthoredValue.SeenKey, board.GetInt(BTProbeAuthoredValue.SeenKey) + 1)

        matched

type BTProbeCountAction() =
    inherit ActionNode()
    let mutable elapsed = 0
    static member TickKey = "BTProbe.ActionTicks"
    static member DoneKey = "BTProbe.ActionCompletions"

    override _.Tick(_, board) =
        board.SetInt(BTProbeCountAction.TickKey, board.GetInt(BTProbeCountAction.TickKey) + 1)
        elapsed <- elapsed + 1

        if elapsed < 3 then
            NodeStatus.Running
        else
            elapsed <- 0
            let doneCount = board.GetInt(BTProbeCountAction.DoneKey) + 1
            board.SetInt(BTProbeCountAction.DoneKey, doneCount)

            if doneCount = 1 then
                BTNode.LogWarning(
                    $"[BTProbe] 시퀀스 완주 — 저작값 확인 {board.GetInt(BTProbeAuthoredValue.SeenKey)}회 · 조건 {board.GetInt(BTProbeAlwaysTrue.CountKey)}회 · 행동 틱 {board.GetInt(BTProbeCountAction.TickKey)}회"
                )

            NodeStatus.Success
