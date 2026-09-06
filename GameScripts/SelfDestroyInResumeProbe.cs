namespace CreatorEngine.Scripts;

/// <summary>
/// LC0 픽스처 — <b>배수 지점에서 자기를 제거한다.</b>
///
/// 앞의 두 자리(<see cref="SelfDestroyInBeginProbe"/> 네이티브 드레인,
/// <see cref="SelfDestroyInTickProbe"/> 관리 순회)와 달리 이 자리는 <b>이번
/// 세션에 새로 생겼다</b>. LC5-b가 <c>await</c> 재개를 프레임 경계의 배수로
/// 모았고, 그 배수는 <c>PrePhysicsTick</c>의 스코프 진행과 <c>PrePhysics</c>
/// 배달 사이에서 돈다.
///
/// 곧 <b>저작 코드가 도는 자리가 하나 늘었다.</b> 그런데 그것을 재는 게이트는
/// 아직 없었다 — 새 실행 지점을 만들고 그 위에서 무엇이 되는지 재지 않은 셈이라
/// 여기서 메운다.
///
/// ── 무엇이 다른가 ──
///
/// 배수는 <c>_active</c> 순회 <b>밖</b>이므로 목록 인덱스 문제는 없다. 대신
/// 다른 것이 걸린다 — 이 시점에 제거를 요청하면 그 프레임의 <c>PrePhysics</c>
/// 배달이 <b>아직 오지 않았다</b>. 제거 표시가 선 인스턴스에 그 배달이 가는지,
/// 가면 안 되는지가 이 픽스처가 받아 적는 사실이다.
///
/// 축소 삼단이 정확히 한 번씩 오는지는 세 픽스처 모두의 공통 판정이다.
/// </summary>
public sealed partial class SelfDestroyInResumeProbe : Component
{
    private const string Kind = "resume";

    private void Mark(string point)
        => Log($"[LC0] kind={Kind} point={point} frame={FrameCount} tid={Environment.CurrentManagedThreadId}");

    public override void OnInitialized()       => Mark("init");
    public override void OnAddedToScene()      => Mark("added");
    public override void OnEnable()            => Mark("enable");
    public override void OnBeginSimulation()   => Mark("beginhook");
    public override void OnDisable()           => Mark("disable");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    private int _ticks;
    public override void PrePhysics(float tick)
    {
        if (_ticks++ < 6) Mark("pre");
    }

    public override async Task OnSimulate()
    {
        Mark("sim");

        // ConfigureAwait를 쓰지 않는다 — 컨텍스트를 타야 배수 지점에서 재개된다.
        // 그것이 이 픽스처가 재려는 바로 그 자리다.
        await Scope.Delay(0.2f);
        Mark("resumed");

        Entity.Destroy();
        Mark("destroyed");
    }
}
