namespace CreatorEngine.Scripts;

/// <summary>
/// LC4 픽스처 ① — <b>재개 뒤에 자기를 제거한다.</b>
///
/// ── 무엇을 재는가 ──
///
/// <c>SimulationScope.Tick</c>은 <c>_pending</c>을 역순으로 돌며 만기된 대기를
/// 완료시킨다. §2.1의 대역 시험은 완료 continuation이 내부 <c>Cancel()</c>을
/// 불러 <c>_pending.Clear()</c>가 돌면 그 다음 인덱스 접근이
/// <c>ArgumentOutOfRangeException</c>이 되는 것을 재현했다. 그 시험은 내부
/// 메서드를 <b>직접</b> 불렀다 — 저작 표면에서 같은 일이 되는지는 재지 않았다.
///
/// 이 픽스처가 그것을 잰다. 저작자가 쓸 수 있는 수단만으로 창을 열어 본다.
///
/// ── 창이 열리려면 ──
///
/// ① <b>재개가 Tick 루프 안에서, 게임 스레드에서 일어나야 한다.</b>
/// ② <b>루프에 아직 방문할 항목이 남아 있어야 한다.</b> 대기 둘을 만들고
///    <b>나중에</b> 만든 것을 먼저 기다린다 — 역순 순회가 그것을 먼저 방문하므로
///    재개가 도는 동안 인덱스 0이 남아 있다.
///
/// ②는 이 픽스처가 만든다. ①은 만들 수단이 없다는 것이 실측 결과다.
///
/// ── 실측 (2026-09-05) ──
///
/// <c>ConfigureAwait(false)</c>는 LC5-b의 컨텍스트를 포기하는 <b>유일한</b> 저작
/// 수단이다. 그런데 그것이 재개를 인라인으로 돌리지 않고 <b>스레드 풀로</b>
/// 보냈다 — 게임 스레드 <c>tid=2</c>에 대해 재개는 <c>tid=4</c>·<c>6</c>이었다.
///
/// 그래서 저작 표면에는 두 길뿐이다:
///
///   컨텍스트를 탄다        → 재개가 프레임 경계로. Tick 루프 밖이다.
///   ConfigureAwait(false)  → 재개가 워커로.       Tick 루프 밖이다.
///
/// 어느 쪽도 <c>Tick</c> 안이 아니다. <b>§2.1의 창은 저작 표면에서 열리지
/// 않는다.</b> 지연 파괴 규약 때문이 아니라 인라인 재개 경로 자체가 없어서다.
///
/// 그러므로 <c>tid</c>가 이 픽스처의 판정 축이다. 언젠가 재개가 게임 스레드로
/// 인라인되면 창이 다시 열리고 LC4를 닫아야 한다 — 게이트가 그때 붉어진다.
///
/// ── 부수 관측 ──
///
/// 재개가 워커이므로 그 뒤의 <c>Entity.Destroy()</c>는 LC5-c의 진입 검사에
/// 걸려 <b>거부</b>된다. 파괴가 일어나지 않으므로 이 인스턴스는 재생 정지까지
/// 살아 있고, 축소 삼단을 대조군과 같은 프레임에 받는다.
/// </summary>
public sealed partial class ReentrantDestroyProbe : Component
{
    private const string Kind = "destroy";

    // tid가 이 픽스처의 핵심이다 — 재개가 Tick 루프 안(게임 스레드)에서 도는지
    // 아니면 워커로 나갔는지가 LC4 창의 개폐를 가른다.
    private void Mark(string point)
        => Log($"[LC4] kind={Kind} point={point} tid={Environment.CurrentManagedThreadId}");

    public override void OnBeginSimulation()   => Mark("begin");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    public override async Task OnSimulate()
    {
        Mark("sim");

        Task first = Scope.Delay(0.2f);    // _pending[0]
        Task second = Scope.Delay(0.2f);   // _pending[1] — 역순 순회가 먼저 본다

        await second.ConfigureAwait(false);
        Mark("inline");

        // 재개가 게임 스레드였다면 여기가 Tick 루프 한복판이다.
        Entity.Destroy();
        Mark("acted");

        await first;
        Mark("resumed");
    }
}
