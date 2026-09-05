namespace CreatorEngine.Scripts;

/// <summary>
/// LC2 픽스처 — <b>정리 콜백 하나가 던지면 종료 절차가 어디까지 가는가.</b>
///
/// ── 무엇을 재는가 ──
///
/// 축소는 <c>DispatchLifecycle</c>의 <c>OnEndSimulation</c> 갈래에서 이 순서로 돈다:
///
/// <code>
/// b.MarkTeardownDelivered();
/// ApplyEnabled(b, false);          // OnDisable
/// b.Scope.Cancel();                // ← 여기서 던지면
/// Invoke(b, x =&gt; x.OnEndSimulation(), ...);   // ← 이 줄이 통째로 안 돈다
/// </code>
///
/// <c>Scope.Cancel()</c>은 <c>Invoke</c>로 감싸여 있지 않다. 그리고 <c>Cancel</c>
/// 본문도 <c>_cts.Cancel()</c> 뒤에 목록 비우기·Dispose·새 토큰 생성이 이어지므로,
/// 콜백 예외가 그 셋도 함께 건너뛰게 만든다.
///
/// ── 두 가지를 갈라서 본다 ──
///
/// ① <b>형제 콜백</b>은 살아남는가. <c>CancellationTokenSource.Cancel()</c>은
///    인자 없이 부르면 <c>throwOnFirstException: false</c>라 등록된 콜백을 전부
///    돌린 뒤 <c>AggregateException</c>으로 모아 던진다. 그러니 형제는 돈다.
/// ② <b>호출자 쪽 후속 생명주기</b>는 살아남는가. 이쪽이 끊긴다.
///
/// 계획서가 "그 둘은 별개의 문제"라고 적어 둔 자리이고, 이 픽스처가 그것을
/// 실엔진에서 갈라 보여 준다.
///
/// ── 기대 ──
///
/// 고침(LC2)이 착지하면 <c>cleanup1</c>·<c>cleanup3</c>가 돌고
/// <c>OnEndSimulation</c>도 와야 한다. <b>지금은 OnEndSimulation이 오지 않는다.</b>
/// </summary>
public sealed partial class FailCleanupProbe : Component
{
    private const string Kind = "failcleanup";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public override void OnInitialized()  => Mark("OnInitialized");
    public override void OnAddedToScene() => Mark("OnAddedToScene");
    public override void OnEnable()       => Mark("OnEnable");

    public override void OnBeginSimulation()
    {
        Mark("OnBeginSimulation");

        // 셋을 거는 이유: 콜백 실행 순서(대개 LIFO)에 판정이 기대지 않게 하려는 것이다.
        // 던지는 것이 가운데 있으므로 앞뒤 어느 쪽이 먼저 돌든 형제 둘은 관측된다.
        Scope.RegisterCleanup(() => Mark("cleanup1"));
        Scope.RegisterCleanup(() => throw new InvalidOperationException("[LC2] 의도한 실패 — 정리 콜백이 던진다"));
        Scope.RegisterCleanup(() => Mark("cleanup3"));
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
