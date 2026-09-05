namespace CreatorEngine.Scripts;

/// <summary>
/// LC1 픽스처 ① — <b>OnInitialized가 던지면 그 뒤 단계가 오는가.</b>
///
/// ── 무엇을 재는가 ──
///
/// <c>ScriptRegistry.DispatchLifecycle</c>은 <c>MarkInitialized()</c>를 **먼저**
/// 세우고 훅을 부른다. <c>Invoke</c>는 예외를 잡아 로그만 남기고 성공 여부를
/// 호출자에게 돌려주지 않으므로, 초기화가 실패해도 "초기화됐다"는 상태가 그대로
/// 남는다. 그 뒤 단계들은 성공한 초기화를 전제로 진행한다.
///
/// 실제 코드에서 이것이 어떤 모습인지가 중요하다 — 생성자에서 잡아 둔 자원이
/// 없는 채로 OnBeginSimulation·OnSimulate가 돌면, 그 안의 null 참조는 원인
/// 지점에서 아주 멀리 떨어진 곳에서 터진다.
///
/// ── 기대 ──
///
/// 고침(LC1)이 착지하면 이 스크립트는 <c>OnInitialized</c> 한 줄만 남기고
/// 조용해야 한다. <b>지금은 그 뒤 훅이 전부 온다</b> — 게이트가 그것을 붉게 잡는다.
///
/// 로그 한 줄 형식은 형제 픽스처와 같다:
/// <code>[LC1] kind=이름 hook=훅 owner=오브젝트</code>
/// </summary>
public sealed partial class FailInitializedProbe : Component
{
    private const string Kind = "failinit";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public override void OnInitialized()
    {
        Mark("OnInitialized");
        throw new InvalidOperationException("[LC1] 의도한 실패 — 초기화가 자원을 잡지 못했다");
    }

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
        try { await Scope.Delay(9999f); }
        catch (OperationCanceledException) { Mark("OnSimulateCancel"); }
    }
}
