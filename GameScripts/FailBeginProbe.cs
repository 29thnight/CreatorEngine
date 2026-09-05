namespace CreatorEngine.Scripts;

/// <summary>
/// LC1 픽스처 ② — <b>OnBeginSimulation이 던져도 루틴이 시작되는가.</b>
///
/// ── 무엇을 재는가 ──
///
/// <c>DispatchLifecycle</c>의 <c>OnBeginSimulation</c> 갈래는 이렇게 생겼다:
///
/// <code>
/// if (b.Enabled)
/// {
///     Invoke(b, static x =&gt; x.OnBeginSimulation(), ...);
///     StartSimulation(b);
/// }
/// </code>
///
/// <c>Invoke</c>가 예외를 삼키고 <c>StartSimulation</c>이 **조건 재검사 없이**
/// 이어진다. 시작 훅이 실패했다는 사실이 루틴 시작 결정에 전혀 반영되지 않는다.
///
/// 형제 픽스처 <see cref="DisableInBeginProbe"/>가 같은 자리를 반대쪽에서 민다 —
/// 이쪽은 예외로, 그쪽은 정상적인 자기 비활성화로. 둘을 갈라 두는 이유는 고침의
/// 방향이 다를 수 있어서다(예외는 격리, 자기 비활성화는 존중).
///
/// ── 기대 ──
///
/// 고침(LC1)이 착지하면 <c>OnSimulate</c> 줄이 없어야 한다.
/// <b>지금은 온다.</b>
/// </summary>
public sealed partial class FailBeginProbe : Component
{
    private const string Kind = "failbegin";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    public override void OnInitialized()       => Mark("OnInitialized");
    public override void OnAddedToScene()      => Mark("OnAddedToScene");
    public override void OnEnable()            => Mark("OnEnable");

    public override void OnBeginSimulation()
    {
        Mark("OnBeginSimulation");
        throw new InvalidOperationException("[LC1] 의도한 실패 — 시작 훅이 준비를 마치지 못했다");
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
