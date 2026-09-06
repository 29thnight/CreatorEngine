namespace CreatorEngine.Scripts;

/// <summary>
/// LC5 픽스처 ① — <b>루틴이 첫 await 전에 던지면.</b>
///
/// 짝인 <see cref="FailSimulateAsyncProbe"/>와 나란히 봐야 뜻이 있다. 같은
/// 논리적 실패("본문이 죽었다")를 발생 형태만 달리해 두 번 낸다.
///
/// ── 왜 async가 아닌가 ──
///
/// <c>async Task</c>로 선언하면 첫 await 전의 예외도 컴파일러가 **faulted Task로
/// 감싸서 반환**한다 — 그러면 동기 throw를 재는 것이 아니라 짝 픽스처와 같은
/// 것을 두 번 재게 된다. 동기 경로를 실제로 태우려면 <c>async</c>를 빼고 직접
/// 던져야 한다.
///
/// ── 오늘의 정책 ──
///
/// <c>StartSimulation</c>의 try/catch가 이것을 잡아 로그를 남기고
/// <c>b.Enabled = false</c>로 **끈다**. 그래서 <c>OnDisable</c>이 온다.
/// 짝 픽스처는 로그만 남고 꺼지지 않는다 — 그 비대칭이 LC5의 판정이다.
/// </summary>
public sealed partial class FailSimulateSyncProbe : Component
{
    private const string Kind = "failsimsync";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    // 실패 뒤에도 틱을 받는가. 이것이 LC5의 판정 축이다.
    //
    // OnDisable이 왔는지로 재면 안 된다 — 축소가 ApplyEnabled(b, false)로
    // 모든 인스턴스에 OnDisable을 주므로(2026-09-05), 재생 중에 꺼진 것과
    // 정지가 끈 것이 한 축에서 구분되지 않는다. 실제로 그 축으로 재 봤더니
    // 두 픽스처가 나란히 초록이었다 — 판별력 0인 거짓 초록이다.
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

    // async가 아니다 — 위 주석 참고.
    public override Task OnSimulate()
    {
        Mark("OnSimulate");
        throw new InvalidOperationException("[LC5] 의도한 실패 — 첫 await 전 동기 예외");
    }
}
