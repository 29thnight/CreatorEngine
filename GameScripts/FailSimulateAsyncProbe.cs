namespace CreatorEngine.Scripts;

/// <summary>
/// LC5 픽스처 ② — <b>루틴이 await 뒤에 던지면.</b>
///
/// <see cref="FailSimulateSyncProbe"/>의 짝이다. 같은 논리적 실패를 발생 형태만
/// 달리해 낸다 — 이쪽은 첫 await를 지난 뒤라 예외가 <b>faulted Task</b>로 나온다.
///
/// ── 오늘의 정책 ──
///
/// <c>StartSimulation</c>은 미완료 Task에 <c>ContinueWith</c>를 걸고
/// <c>ReportSimulationFault</c>가 **로그만** 남긴다. 인스턴스는 켜진 채로 남아
/// 다음 프레임부터 틱을 계속 받는다.
///
/// 짝 픽스처는 같은 실패에 대해 꺼진다. 그 차이가 LC5의 "동기/Task 실패 정책
/// 불일치"이고, 이 둘을 나란히 두는 것이 그것을 재는 유일한 방법이다 — 어느
/// 한쪽만으로는 "이 정책이 의도된 것인가"를 물을 수조차 없다.
///
/// ── 기대 ──
///
/// 고침(LC5)이 착지하면 두 픽스처가 **같은 상태로** 끝나야 한다. 어느 쪽으로
/// 통일할지(둘 다 끈다 / 둘 다 로그만)는 LC5가 정한다 — 이 게이트는 다르다는
/// 사실만 붉게 잡는다.
/// </summary>
public sealed partial class FailSimulateAsyncProbe : Component
{
    private const string Kind = "failsimasync";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    // 짝 픽스처와 같은 계측. 실패 뒤에도 틱을 받는지가 판정 축이다 —
    // 자세한 이유는 <see cref="FailSimulateSyncProbe"/>의 같은 자리에 적었다.
    private int _beats;

    public override void PostPhysics(float tick)
    {
        if (0 == ++_beats % 30) Mark("beat");
    }

    public override void OnInitialized()       => Mark("OnInitialized");
    public override void OnAddedToScene()      => Mark("OnAddedToScene");
    public override void OnEnable()            => Mark("OnEnable");
    public override void OnBeginSimulation()   => Mark("OnBeginSimulation");
    public override void OnDisable()           => Mark("OnDisable");
    public override void OnEndSimulation()     => Mark("OnEndSimulation");
    public override void OnRemovingFromScene() => Mark("OnRemovingFromScene");
    public override void OnUninitializing()    => Mark("OnUninitializing");

    public override async Task OnSimulate()
    {
        Mark("OnSimulate");

        // 짧게 한 번 쉬어 첫 await를 확실히 지난다. 이 뒤의 예외는 컴파일러가
        // faulted Task로 감싸 반환하므로 동기 경로가 아니라 Task 경로를 탄다.
        await Scope.Delay(0.1f);
        Mark("OnSimulateResume");

        throw new InvalidOperationException("[LC5] 의도한 실패 — await 뒤 Task 예외");
    }
}
