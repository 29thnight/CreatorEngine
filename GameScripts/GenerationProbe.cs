namespace CreatorEngine.Scripts;

/// <summary>
/// LC7 픽스처 — <b>재생을 두 번 하면 세대가 섞이는가.</b>
///
/// ── 왜 이 시나리오인가 ──
///
/// 에디터에서 재생·정지는 저작 중 가장 자주 하는 일이다. 그런데 기존 게이트는
/// 전부 <b>재생 한 번</b>만 태운다 — 두 번째 재생에서 무엇이 되는지는 어느
/// 게이트도 보지 않는다.
///
/// 재생 정지는 씬을 백업에서 되살리는 방식이라 DontDestroyOnLoad를 뺀 전
/// 오브젝트가 파괴 경로를 지난다(설계 문서 §4 트랙 L1). 즉 <b>인스턴스가 통째로
/// 새로 만들어진다</b>. 그 경계에서 새 나갈 수 있는 것들:
///
///   · 1세대의 대기가 2세대에서 재개된다(스코프 토큰이 새로 열리지 않았다)
///   · 1세대가 축소를 두 번 받거나 한 번도 못 받는다
///   · 2세대가 1세대의 상태를 물려받는다
///
/// ── 세대를 어떻게 세는가 ──
///
/// 정적 카운터로 <b>프로세스 수명 동안 만들어진 인스턴스</b>를 센다. 어셈블리가
/// 리로드되지 않는 한 이 값은 재생을 건너 이어지므로, 1세대와 2세대의 기록이
/// <c>id</c>로 갈린다. 인스턴스 자신의 필드로는 셀 수 없다 — 새로 만들어지면
/// 초기값으로 돌아가기 때문이다.
///
/// ── 재개 표지가 판정의 핵심이다 ──
///
/// <c>OnSimulate</c>가 긴 대기를 걸어 두고 그 뒤에 표지를 남긴다. 정지가 스코프를
/// 취소하므로 그 표지는 <b>영영 오지 않아야</b> 한다. 온다면 1세대의 대기가 살아
/// 남아 2세대까지 흘렀다는 뜻이다.
///
/// 취소로 풀리는 것은 예외 경로다. <c>catch</c>로 받아 표지를 남기면 "취소가
/// 제대로 왔다"까지 함께 관측된다 — 표지가 없는 것과 취소된 것을 가를 수 있다.
/// </summary>
public sealed partial class GenerationProbe : Component
{
    /// <summary>프로세스 수명 동안 만들어진 인스턴스 수. 재생을 건너 이어진다.</summary>
    private static int _created;
    private static int _recordedHooks;
    [EngineCallable] public static int RecordedHooks() => _recordedHooks;

    private readonly int _id;

    public GenerationProbe() => _id = ++_created;

    private void Mark(string point)
    {
        ++_recordedHooks;
        Log($"[LC7] id={_id} point={point} frame={FrameCount}");
    }

    public override void OnInitialized()       => Mark("init");
    public override void OnAddedToScene()      => Mark("added");
    public override void OnEnable()            => Mark("enable");
    public override void OnBeginSimulation()   => Mark("begin");
    public override void OnDisable()           => Mark("disable");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    public override async Task OnSimulate()
    {
        Mark("sim");

        try
        {
            // 재생 구간보다 확실히 긴 대기. 정지가 이것을 취소해야 한다.
            await Scope.Delay(30f);

            // 여기 오면 1세대의 대기가 취소를 지나 살아남았다는 뜻이다.
            Mark("leaked");
        }
        catch (OperationCanceledException)
        {
            Mark("cancelled");
        }
    }
}
