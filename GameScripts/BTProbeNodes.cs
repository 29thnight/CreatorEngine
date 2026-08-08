using CreatorEngine;

namespace GameScripts;

// 행동 트리 검증 전용 노드 (PHASE 9-8).
//
// ── 왜 전용 노드인가 ──
//
// 기존 BT 에셋의 잎 노드는 사용자가 쓴 Action/Condition 43종을 이름으로 참조하는데,
// 9-4가 C++ 스크립트 경로를 은퇴시킨 뒤 그중 C#으로 옮겨진 것이 없다. 그 이식이 끝날
// 때까지 기다리면 BT 경로는 아무 검증 없이 남는다.
//
// 그렇다고 기존 이름(IsDaed·DaedAction 등)을 스텁으로 채우면 안 된다 — 그 순간 실제
// 게임 AI가 스텁 동작을 하게 되고, 이식된 줄 알고 넘어가기까지 한다. 검증을 위해
// 게임 동작을 바꾸는 것은 거꾸로다.
//
// 그래서 이름을 겹치지 않게 두고(BTProbe 접두사) 검증 전용 그래프에서만 쓴다.
// 이 노드들이 하는 일은 '몇 번 불렸는지'를 블랙보드에 남기는 것뿐이라, 트리가 실제로
// 순회됐는지를 밖에서 확인할 수 있다.

/// <summary>
/// 항상 참. 조건 잎이 실제로 평가되는지를 센다.
///
/// 순회 증거를 블랙보드에 남기는 이유: 트리가 섰다는 것과 트리가 돌았다는 것은 다르고,
/// 노드가 아무 흔적을 남기지 않으면 밖에서 그 둘을 가를 수 없다.
/// </summary>
public sealed class BTProbeAlwaysTrue : ConditionNode
{
    public const string CountKey = "BTProbe.ConditionCount";

    public override bool ConditionCheck(float deltaTime, BlackBoard blackBoard)
    {
        blackBoard.SetInt(CountKey, blackBoard.GetInt(CountKey) + 1);
        return true;
    }
}

/// <summary>
/// 정해진 횟수만큼 Running을 돌려준 뒤 Success로 끝나는 행동.
///
/// Running을 거치는 것이 핵심이다 — BT는 그 상태를 프레임 간에 이어 가고, 그 상태가
/// 트리 노드 안에 있다. 즉 이 노드가 제대로 동작한다는 것은 관리 측 트리가 프레임을
/// 넘어 살아 있다는 뜻이기도 하다(트리가 매 프레임 다시 세워진다면 영원히 첫 틱이다).
/// </summary>
public sealed class BTProbeCountAction : ActionNode
{
    public const string TickKey = "BTProbe.ActionTicks";
    public const string DoneKey = "BTProbe.ActionCompletions";

    private const int RunningTicks = 3;

    private int _elapsed;

    public override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        blackBoard.SetInt(TickKey, blackBoard.GetInt(TickKey) + 1);

        if (++_elapsed < RunningTicks) return NodeStatus.Running;

        // 다음 순회에서 다시 처음부터 세도록 되돌린다. 되돌리지 않으면 한 번 끝난 뒤
        // 계속 즉시 Success라, Running 경로가 한 번만 검증되고 만다.
        _elapsed = 0;
        int done = blackBoard.GetInt(DoneKey) + 1;
        blackBoard.SetInt(DoneKey, done);

        // 첫 완주에서만 한 줄 남긴다. 매번 남기면 프레임마다 쌓여 로그가 쓸모없어지고,
        // 아예 안 남기면 밖에서 트리가 끝까지 갔는지 알 수 없다.
        //
        // 이 한 줄이 네 가지를 동시에 증명한다 — 저작 블랙보드가 실려 왔고(AuthoredSeen),
        // 조건 잎이 평가됐고, 행동이 Running을 거쳐 Success에 닿았고, 자식 순서가
        // 지켜졌다(행동이 시퀀스의 마지막이라 앞의 조건 둘이 통과해야만 여기 온다).
        if (done == 1)
        {
            LogWarning($"[BTProbe] 시퀀스 완주 — 저작값 확인 {blackBoard.GetInt(BTProbeAuthoredValue.SeenKey)}회 · " +
                       $"조건 {blackBoard.GetInt(BTProbeAlwaysTrue.CountKey)}회 · 행동 틱 {blackBoard.GetInt(TickKey)}회");
        }

        return NodeStatus.Success;
    }
}

/// <summary>
/// 저작된 블랙보드 값을 읽는 조건.
///
/// 이것이 참이려면 네이티브가 저작 블랙보드를 실어 보냈고(B3의 FlattenBlackBoard)
/// 관리 측이 그것을 초기 상태로 채웠어야 한다. 그 배선이 끊기면 값이 기본값(0)이
/// 되는데, 그 차이는 크래시가 아니라 'AI가 좀 이상하다'로만 나타나는 종류라 여기서
/// 명시적으로 잡는다.
/// </summary>
public sealed class BTProbeAuthoredValue : ConditionNode
{
    public const string AuthoredKey = "BTProbe.Authored";
    public const string SeenKey     = "BTProbe.AuthoredSeen";

    /// <summary>에셋에 저작해 둔 값. 이 숫자가 그대로 읽혀야 한다.</summary>
    public const int Expected = 4242;

    public override bool ConditionCheck(float deltaTime, BlackBoard blackBoard)
    {
        bool matched = blackBoard.GetInt(AuthoredKey) == Expected;
        if (matched) blackBoard.SetInt(SeenKey, blackBoard.GetInt(SeenKey) + 1);
        return matched;
    }
}
