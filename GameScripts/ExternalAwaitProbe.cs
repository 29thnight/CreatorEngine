namespace CreatorEngine.Scripts;

/// <summary>
/// LC5-b 픽스처 — <b>await가 재개되는 스레드.</b>
///
/// LC5의 앞 갈래는 "실패가 어떤 상태를 남기는가"를 맞췄다. 이쪽은 그보다 앞선
/// 질문이다 — <b>재개된 본문이 애초에 어느 스레드에서 도는가.</b>
///
/// ── 왜 한 픽스처에 두 경로를 넣는가 ──
///
/// 지원 경로(<c>Scope.Delay</c>)와 미지원 경로(외부 Task)를 <b>같은 인스턴스의
/// 같은 루틴 안에서</b> 잇달아 잰다. 따로 두면 "외부 await가 워커에서 깬다"를
/// 관측해도 그것이 이 엔진의 규약 위반인지, 아니면 관리 측 재개가 원래 다
/// 워커인지 가릴 수 없다. 지원 경로가 게임 스레드로 깨는 것을 바로 옆에서 함께
/// 보면 그 둘이 갈린다.
///
/// <c>worker</c> 표지를 따로 남기는 이유도 같다. <c>Task.Run</c>이 어떤 사정으로
/// 호출 스레드에서 인라인되면 <c>external</c>이 게임 스레드와 같아져 <b>고쳐진
/// 것처럼</b> 보인다. 워커 몸통이 실제로 다른 스레드였다는 증거를 함께 남겨야
/// 그 거짓 초록이 막힌다.
///
/// ── 오늘의 값 ──
///
/// 관리 측에 SynchronizationContext가 없다(실측: <c>ScriptCore</c> 전체에 0건).
/// 그래서 <c>await</c>는 포착할 컨텍스트가 없어 <c>TaskScheduler.Default</c> —
/// 곧 스레드 풀 — 로 이어진다. 지원 경로가 오늘 안전한 것은 계약 때문이 아니라
/// <c>TaskCompletionSource</c>의 기본값이 <b>인라인 완료</b>라서다. 완료를 일으키는
/// <c>SimulationScope.Tick</c>이 게임 스레드에 있으니 그 자리에서 이어질 뿐이다.
/// 누군가 <c>RunContinuationsAsynchronously</c>를 붙이면 지원 경로도 조용히
/// 워커로 옮겨간다 — 오늘의 안전은 기본값 하나에 얹혀 있다.
///
/// ── 기대 ──
///
/// 고침(LC5-b)이 착지하면 <c>delay</c>와 <c>external</c>이 <b>둘 다</b> 게임
/// 스레드여야 한다. 외부 await를 금지해서가 아니라 완료를 프레임 경계로 넘겨서다.
///
/// ── 이 픽스처가 일부러 하지 않는 것 ──
///
/// 재개 직후에 엔진 상태를 <b>쓰지</b> 않는다. 워커에서 씬 그래프에 쓰면 접근
/// 위반으로 하네스째 죽을 수 있고, 그러면 정작 증거가 될 로그가 flush 전에
/// 사라진다. 다만 <c>Mark</c> 자체가 <c>Native.Log</c>를 타므로 <b>경계를 넘는
/// 호출은 이미 일어나고 있다</b> — 관측을 위해 따로 만든 위험이 아니다.
/// </summary>
public sealed partial class ExternalAwaitProbe : Component
{
    private void Mark(string point)
        => Log($"[LC5b] point={point} tid={Environment.CurrentManagedThreadId} owner={Entity.Name}");

    // 게임 스레드의 기준값. 이 훅은 ScriptRegistry가 프레임 안에서 직접 부르므로
    // 정의상 게임 스레드다 — 판정은 이 값을 정본으로 삼는다(Native 내부의
    // _gameThreadId를 읽지 않는다. 그것을 읽으면 고침의 자기 신고를 그대로
    // 베끼는 셈이라 대조가 되지 않는다).
    public override void OnBeginSimulation() => Mark("begin");

    public override async Task OnSimulate()
    {
        Mark("sim");

        // ① 지원 경로 — 엔진 dt로 흐르는 대기.
        await Scope.Delay(0.1f);
        Mark("delay");

        // ② 미지원 경로 — 엔진이 모르는 외부 Task.
        //    Delay(50ms)로 확실히 비동기 완료를 만든다. 즉시 반환하면 await가
        //    동기로 지나가 재개 스레드를 아예 재지 못한다.
        await Task.Run(async () =>
        {
            await Task.Delay(50);
            Log($"[LC5b] point=worker tid={Environment.CurrentManagedThreadId} owner=-");
        });

        Mark("external");
    }
}
