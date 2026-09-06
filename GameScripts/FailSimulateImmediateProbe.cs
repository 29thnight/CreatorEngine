namespace CreatorEngine.Scripts;

/// <summary>
/// LC5 픽스처 ③ — <b>async 루틴이 첫 await 전에 던지면.</b>
///
/// 세 자매 중 가운데다. 계획서가 "직접 throw · 즉시 faulted Task · await 이후
/// faulted Task"를 세 종류로 셌는데, 처음에는 이 가운데가 빠져 있었다.
///
/// ── 왜 이것이 별개인가 ──
///
/// <see cref="FailSimulateSyncProbe"/>와 코드 모양이 거의 같다(첫 await 전 throw).
/// 그런데 <c>async</c>가 붙는 순간 컴파일러가 예외를 **이미 완료된 faulted Task**로
/// 감싸 반환한다. 그래서 호출자에게는:
///
///   동기 throw      → <c>b.OnSimulate()</c> 호출 자체가 던진다 (try/catch가 잡는다)
///   즉시 faulted    → 정상 반환하는데 <c>task.IsCompleted</c>가 참이고 faulted다
///   await 뒤 faulted → 미완료 Task로 반환된 뒤 나중에 faulted가 된다
///
/// 세 갈래가 <c>StartSimulation</c>의 서로 다른 세 자리로 흘러간다. 실제 스크립트
/// 저작에서 <c>async</c>를 붙이고 안 붙이고는 취향에 가까운데, 그것으로 엔진의
/// 실패 정책이 갈리면 안 된다.
///
/// ── 기대 ──
///
/// 고침(LC5)이 착지하면 세 픽스처가 **같은 상태로** 끝나야 한다.
/// </summary>
public sealed partial class FailSimulateImmediateProbe : Component
{
    private const string Kind = "failsimimm";

    private void Mark(string hook) => Log($"[LC1] kind={Kind} hook={hook} owner={Entity.Name}");

    // 자매 픽스처와 같은 계측 — 실패 뒤에도 틱을 받는지가 판정 축이다.
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

#pragma warning disable CS1998 // await가 없는 async — 즉시 faulted Task를 만드는 것이 이 픽스처의 목적이다
    public override async Task OnSimulate()
    {
        Mark("OnSimulate");
        throw new InvalidOperationException("[LC5] 의도한 실패 — async 첫 await 전 예외(즉시 faulted Task)");
    }
#pragma warning restore CS1998
}
