namespace CreatorEngine.Scripts;

/// <summary>
/// LC7 픽스처 — <b>DontDestroyOnLoad 이송을 대기가 건너는가.</b>
///
/// ── 왜 이 시나리오인가 ──
///
/// 설계 문서 §1.2-5가 보존 대상으로 적은 것이다: "DDOL 이동은 씬 Removing/Added
/// 통지를 주며 <b>인스턴스와 시뮬레이션을 유지한다.</b> detach를 파괴·취소로
/// 취급하거나 Initialized/Begin을 다시 전달하지 않는다."
///
/// 기존 <c>verify-ddol-script</c>는 그 문장의 <b>앞 절반</b>만 잰다 — 통지가 왔는가,
/// 몇 번 왔는가. 뒤 절반(시뮬레이션이 유지되는가)은 어느 게이트도 보지 않았다.
/// 그런데 이송은 씬 그래프에서 오브젝트를 떼어 다른 씬에 붙이는 일이라, 떼는 쪽을
/// 파괴로 오독하면 스코프가 취소되고 <c>OnSimulate</c> 본문이 그 자리에서 풀린다.
/// 통지는 정확한데 루틴만 죽는 상태가 가능하고, 그것이 지금 관측 밖이다.
///
/// ── 어떻게 재는가 ──
///
/// 이송을 <b>건너도록</b> 긴 대기를 걸어 두고, 그 대기가 어떻게 끝났는지를 세 갈래로
/// 가른다.
///
///   resumed    정상 완료 — 이송이 시뮬레이션을 건드리지 않았다
///   cancelled  스코프가 취소됐다 — detach가 파괴로 취급됐다
///   (없음)     대기가 영영 풀리지 않았다 — 이송 뒤 스코프 틱이 멎었다
///
/// 세 번째가 표지 부재로만 드러나므로, 앞의 둘을 <b>둘 다</b> 표지로 남긴다.
/// "cancelled 가 없다"만으로는 세 번째와 구별되지 않는다.
///
/// 단계 재전달도 함께 센다. 이송이 새 진입으로 오독되면 <c>OnInitialized</c>나
/// <c>OnBeginSimulation</c>이 두 번 오고, <c>OnSimulate</c>도 다시 시작된다 —
/// 그러면 같은 컴포넌트가 루틴 둘을 동시에 돌린다.
/// </summary>
public sealed partial class DdolWaitProbe : Component
{
    /// <summary>
    /// 이송 시점을 건널 만큼. 시나리오가 이송을 이 안에 넣는다.
    ///
    /// 헤드리스 프레임이 얼마나 빠른지 모르는 채로 초를 고르면 안 된다 — 너무
    /// 짧으면 이송 전에 재개가 끝나고, 너무 길면 시나리오의 wait 가 모자라 "죽었다"와
    /// "아직이다"를 못 가른다. 아래 dt 진단이 그 환산을 로그에 남긴다.
    /// </summary>
    private const float CrossSeconds = 1.0f;

    private bool _dtReported;

    private void Mark(string point) => Log($"[LC7c] point={point} frame={FrameCount}");

    /// <summary>
    /// 프레임당 dt 를 한 번만 남긴다. 게이트가 "대기가 몇 프레임짜리인지"를 로그만
    /// 보고 알 수 있어야, 재개 표지가 없을 때 그것이 결함인지 시나리오의 wait 부족인지
    /// 가를 수 있다.
    /// </summary>
    public override void PostPhysics(float tick)
    {
        if (_dtReported) return;
        _dtReported = true;
        Log($"[LC7c] point=dt value={tick:0.#####} cross={CrossSeconds} frames≈{(tick > 0f ? CrossSeconds / tick : -1f):0}");
    }

    public override void OnInitialized()       => Mark("init");
    public override void OnAddedToScene()      => Mark("added");
    public override void OnBeginSimulation()   => Mark("begin");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnUninitializing()    => Mark("uninit");

    public override async Task OnSimulate()
    {
        Mark("sim");

        try
        {
            await Scope.Delay(CrossSeconds);

            // 이송을 건너 살아 돌아왔다. Entity 를 여기서 만지는 이유는, 대기만
            // 풀리고 핸들이 죽어 있으면 "유지됐다"고 말할 수 없기 때문이다.
            Mark($"resumed name={Entity.Name}");
        }
        catch (OperationCanceledException)
        {
            // 이송이 파괴로 취급됐다. 정지에서 오는 취소와 구별하기 위해 이 표지는
            // 이송 구간에서만 의미를 갖는다 — 게이트가 프레임으로 가른다.
            Mark("cancelled");
        }
    }
}
