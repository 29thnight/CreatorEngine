namespace CreatorEngine.Scripts;

/// <summary>
/// LC4 픽스처 ② — <b>재개 뒤에 네이티브를 왕복한다.</b>
///
/// <see cref="ReentrantDestroyProbe"/>와 창을 여는 방법은 같다(대기 둘 · 나중
/// 것을 먼저 · <c>ConfigureAwait(false)</c>). 다른 것은 그 뒤에 하는 일이다.
///
/// ── 왜 비활성화가 파괴와 다른 시험인가 ──
///
/// <c>Enabled = false</c>는 관리 측에서 끝나지 않는다. <c>80db6b36</c> 이후 이
/// setter는 <c>Native.ScriptSetEnabled</c>로 내려가고, 네이티브
/// <c>Component::SetEnabled</c>가 전이일 때 <c>OnDisable</c>을 다시 관리 측으로
/// 올려 보낸다. 곧 <b>경계를 두 번 건넌 뒤 사용자 훅이 돈다</b> — 파괴가 지연
/// 규약에 막혀 아무 일도 안 하더라도 이 경로는 실제로 무언가를 한다.
///
/// ── 이 픽스처가 결함 하나를 물어 왔다 (2026-09-05) ──
///
/// 재개가 워커라 <c>ScriptSetEnabled</c>는 LC5-c의 진입 검사에 거부된다. 그런데
/// setter는 그 거짓을 <b>전달 실패</b>로 읽고 관리 측 폴백을 탔다 — 워커에서
/// 관리 상태를 바꾸고 <c>OnDisable</c>까지 그 자리에서 돌렸다. 거부가 막으려던
/// 일의 축소판을 관리 측에 다시 만든 셈이다.
///
/// 원인은 <c>Entered()</c>의 거짓이 "표가 없다"와 "스레드 밖이다"를 구분하지
/// 못한 것이다(거부는 일부러 "표가 없을 때"의 길을 탄다). setter가 그 둘을
/// 가르도록 고쳤고, 스레드 밖 거부에는 폴백을 태우지 않는다.
///
/// 그래서 <c>disable</c> 표지의 <b>부재</b>가 판정이다. 그것이 다시 나타나면
/// 거부가 관리 측으로 새고 있다는 뜻이다.
///
/// ── resumed가 와야 하는 이유 ──
///
/// 비활성 컴포넌트도 <c>Scope.Delay</c>는 계속 흐른다(설계 문서 §4 트랙 L2).
/// 게다가 고침 뒤에는 비활성화 자체가 적용되지 않으므로 더욱 그렇다. 남은
/// 대기가 정상 완료되어야 하고 <c>resumed</c>가 반드시 온다.
/// </summary>
public sealed partial class ReentrantDisableProbe : Component
{
    private const string Kind = "disable";

    // tid가 이 픽스처의 핵심이다 — 재개가 Tick 루프 안(게임 스레드)에서 도는지
    // 아니면 워커로 나갔는지가 LC4 창의 개폐를 가른다.
    private void Mark(string point)
        => Log($"[LC4] kind={Kind} point={point} tid={Environment.CurrentManagedThreadId}");

    public override void OnBeginSimulation()   => Mark("begin");
    public override void OnDisable()           => Mark("disable");
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

        // 경계 왕복 시도. 워커이므로 거부되고, 관리 폴백도 타지 않아야 한다.
        Enabled = false;
        Mark("acted");

        await first;
        Mark("resumed");
    }
}
