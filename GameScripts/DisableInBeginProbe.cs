namespace CreatorEngine.Scripts;

/// <summary>
/// LC1 픽스처 ③ — <b>시작 훅 안에서 스스로 꺼져도 루틴이 시작되는가.</b>
///
/// ── 왜 예외와 따로 두는가 ──
///
/// <see cref="FailBeginProbe"/>와 같은 자리를 밀지만 성격이 정반대다. 이쪽은
/// <b>실패가 아니라 정상적인 의사 표현</b>이다 — "조건이 안 맞으니 이번 판에는
/// 돌지 않겠다"는 흔한 형태이고, Unity에서도 통하는 관용구다.
///
/// <c>DispatchLifecycle</c>은 <c>if (b.Enabled)</c>를 훅 **앞에서** 한 번 보고,
/// 훅이 끝난 뒤 <c>StartSimulation</c>을 부르기 전에는 다시 보지 않는다. 그래서
/// 훅 안에서 끈 것이 반영되지 않는다.
///
/// 이 픽스처가 따로 있어야 고침의 방향을 가를 수 있다. 예외는 격리하는 것이
/// 맞지만 자기 비활성화는 **존중**해야 한다 — 둘을 한 축으로 재면 "실패했으니
/// 끈다"와 "스스로 껐다"가 구분되지 않는다.
///
/// ── 기대 ──
///
/// 고침(LC1)이 착지하면 <c>OnDisable</c>까지만 남고 <c>OnSimulate</c>는 없어야
/// 한다. <b>지금은 온다.</b>
/// </summary>
public sealed partial class DisableInBeginProbe : Component
{
    private const string Kind = "disablebegin";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public override void OnInitialized()       => Mark("OnInitialized");
    public override void OnAddedToScene()      => Mark("OnAddedToScene");
    public override void OnEnable()            => Mark("OnEnable");

    public override void OnBeginSimulation()
    {
        Mark("OnBeginSimulation");

        // 예외가 아니다 — 정상 경로로 "이번엔 안 돈다"고 말하는 것이다.
        Enabled = false;
    }

    public override void OnDisable()           => Mark("OnDisable");
    public override void OnEndSimulation()     => Mark("OnEndSimulation");
    public override void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public override void OnUninitializing()    => Mark("OnUninitializing");

    public override async Task OnSimulate()
    {
        Mark("OnSimulate");
        try { await Scope.Delay(9999f); }
        catch (OperationCanceledException) { Mark("OnSimulateCancel"); }
    }
}
