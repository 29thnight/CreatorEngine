namespace CreatorEngine.Scripts;

/// <summary>
/// LC0 픽스처 — <b>시작 훅 안에서 자기를 제거한다.</b>
///
/// ── 자기 제거를 자리별로 나누는 이유 ──
///
/// "자기 제거"는 하나의 시나리오가 아니다. 부르는 자리마다 그 순간 엔진이 무엇을
/// 순회하고 있는지가 다르고, 깨질 수 있는 것도 다르다. 셋으로 나눠 잰다:
///
///   <see cref="SelfDestroyInBeginProbe"/>   네이티브 드레인 한복판
///   <see cref="SelfDestroyInTickProbe"/>    관리 <c>_active</c> 순회 한복판
///   <see cref="SelfDestroyInResumeProbe"/>  배수 지점(LC5-b가 새로 만든 자리)
///
/// ── 이 자리가 특히 날카로운 이유 ──
///
/// <c>DispatchLifecycle(OnBeginSimulation)</c>은 훅을 부른 <b>바로 다음 줄</b>에서
/// <c>StartSimulation</c>을 부른다. LC1이 그 사이에 <c>Enabled</c>·<c>IsAlive</c>
/// 재검사를 넣었지만, 파괴는 지연 규약이라 <b>표시만 서고 아직 살아 있을 수</b>
/// 있다. 그러면 이미 죽기로 정해진 인스턴스에서 루틴이 시작된다.
///
/// 그것이 결함인지 아닌지는 이 픽스처가 정하지 않는다. 무엇이 실제로 오는지를
/// 받아 적고, <b>축소 삼단이 정확히 한 번씩</b> 오는지를 판정한다 — 그것이
/// 자기 제거에서 가장 흔하게 깨지는 불변식이다(정상 경로와 폴백 경로가 둘 다
/// 부르면 두 번, 둘 다 미루면 0번).
/// </summary>
public sealed partial class SelfDestroyInBeginProbe : Component
{
    private const string Kind = "begin";

    private void Mark(string point)
        => Log($"[LC0] kind={Kind} point={point} frame={FrameCount} tid={Environment.CurrentManagedThreadId}");

    public override void OnInitialized()       => Mark("init");
    public override void OnAddedToScene()      => Mark("added");
    public override void OnEnable()            => Mark("enable");
    public override void OnDisable()           => Mark("disable");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    public override void OnBeginSimulation()
    {
        Mark("beginhook");
        Entity.Destroy();
        Mark("destroyed");
    }

    // 루틴이 시작되면 표지가 남는다. 시작되지 않는 것이 옳은 방향이지만, 이
    // 픽스처는 방향을 단정하지 않고 사실만 남긴다.
    public override Task OnSimulate()
    {
        Mark("sim");
        return Task.CompletedTask;
    }

    // 제거를 요청한 뒤에도 틱을 받는지가 축이다. 건수가 아니라 **있는가**가
    // 판정이므로 몇 개만 남긴다 — 제거가 통째로 실패하면 400프레임어치가 쏟아져
    // 다른 진단을 덮는다.
    private int _ticks;
    public override void PostPhysics(float tick)
    {
        if (_ticks++ < 3) Mark("tick");
    }
}
