using System.Collections.Concurrent;
using System.Threading;

namespace CreatorEngine;

/// <summary>
/// await로 흩어진 재개를 게임 스레드의 프레임 경계로 다시 모으는 컨텍스트 (LC5-b · 2026-09-05).
///
/// ── 무엇이 문제였나 ──
///
/// 관리 훅은 전부 게임 스레드 전용이다(ClrHost.h 규약). 그런데 <c>await</c> 한 줄이면
/// 그 규약 밖으로 나간다. 재개 지점은 <c>await</c>가 <b>포착한 컨텍스트</b>가 정하는데
/// 관리 측에 컨텍스트가 하나도 없었다(실측: <c>ScriptCore</c> 전체 0건). 포착할 것이
/// 없으면 <c>TaskScheduler.Default</c> — 곧 스레드 풀 — 로 이어진다. 그 뒤의 본문은
/// 여전히 <c>Transform</c>·<c>Entity</c>를 부르고, 그것들은 함수 포인터로 C++에
/// 곧장 들어간다.
///
/// 실측(<c>verify-lifecycle-thread</c>): <c>await Task.Run(...)</c> 뒤가 tid=4에서
/// 이어졌다. 게임 스레드는 tid=2였다.
///
/// ── 왜 금지가 아니라 마셜링인가 ──
///
/// 외부 <c>await</c>를 타입으로 막는 길도 있다(PHASE 24가 검토한 방향). 그러나 그것은
/// 저작 표면을 바꾸는 일이고, 여기서 닫아야 하는 것은 <b>지금 코드가 이미 할 수 있는
/// 일</b>의 안전성이다. 완료를 프레임 경계로 넘기면 저작자가 무엇을 await하든 재개는
/// 게임 스레드에서 일어난다 — 규약이 저작 규율이 아니라 런타임 성질이 된다.
///
/// ── 지원 경로의 순서를 지키는 법 ──
///
/// 컨텍스트가 서면 <c>await Scope.Delay</c>도 여기를 거친다. 예전에는
/// <c>TaskCompletionSource</c>의 인라인 완료 덕에 <c>SimulationScope.Tick</c> 안에서
/// 그 자리로 이어졌다. 그 순서 — "프레임의 일이 시작되기 전에 재개된다" — 는 지켜야
/// 하므로 <see cref="ScriptRegistry.PrePhysicsTick"/>이 스코프 진행과 <c>PrePhysics</c>
/// 배달 <b>사이</b>에서 배수한다.
///
/// ── 배수를 한 프레임 안에서 묶는 이유 ──
///
/// 진입 시점의 건수만 처리한다. 재개된 본문이 이미 완료된 무언가를 다시 await하면
/// 그 자리에서 또 게시되는데, 그것까지 따라가면 한 프레임이 영영 끝나지 않을 수 있다.
/// 남은 것은 다음 프레임이 가져간다.
/// </summary>
internal sealed class GameThreadSynchronizationContext : SynchronizationContext
{
    private readonly struct Continuation(SendOrPostCallback callback, object? state)
    {
        public readonly SendOrPostCallback Callback = callback;
        public readonly object? State = state;
    }

    private readonly ConcurrentQueue<Continuation> _queue = new();

    /// <summary>지금 게임 스레드에 서 있는 컨텍스트. 없으면 아직 설치 전이다.</summary>
    internal static GameThreadSynchronizationContext? Installed { get; private set; }

    /// <summary>
    /// 게임 스레드에 컨텍스트를 세운다. <see cref="Bootstrap.Initialize"/>가 딱 한 번,
    /// 게임 스레드에서 부른다 — 컨텍스트는 스레드마다 따로이므로 부르는 자리가 곧
    /// 적용 대상이다.
    /// </summary>
    internal static void Install()
    {
        var context = new GameThreadSynchronizationContext();
        SetSynchronizationContext(context);
        Installed = context;
    }

    /// <summary>
    /// 재개를 대기열에 넣는다. 워커에서도 불리므로 큐는 동시 접근을 견뎌야 한다.
    /// </summary>
    public override void Post(SendOrPostCallback d, object? state)
        => _queue.Enqueue(new Continuation(d, state));

    /// <summary>
    /// 동기 실행 요청. 게임 스레드에서 온 것이면 그 자리에서 돌린다.
    ///
    /// 워커에서 오면 <b>거부한다</b>. 이 규약을 지키려면 게임 스레드가 배수할 때까지
    /// 호출자를 막아야 하는데, 그 게임 스레드가 같은 작업의 결과를 기다리고 있으면
    /// 서로를 기다리다 프레임이 통째로 멈춘다. 조용히 비동기로 바꿔 치면 <c>Send</c>가
    /// 약속한 "돌아올 때는 끝나 있다"가 깨져 더 나쁜 자리에서 드러난다. 그래서 막고
    /// 원인을 그 자리에서 알린다 — 지원하지 않는 범위를 명시적으로 거부하는 것이다.
    /// </summary>
    public override void Send(SendOrPostCallback d, object? state)
    {
        if (Native.IsGameThread) { d(state); return; }

        throw new NotSupportedException(
            "게임 스레드 밖에서 SynchronizationContext.Send를 부를 수 없다. " +
            "결과가 필요하면 게임 스레드에서 await로 받아라(재개는 프레임 경계로 돌아온다).");
    }

    /// <summary>
    /// 자식 작업이 컨텍스트를 물려받을 때 쓰인다. 이 컨텍스트는 "게임 스레드"라는
    /// 하나뿐인 대상을 가리키므로 사본이 따로 있을 이유가 없다 — 같은 큐를 공유해야
    /// 어디서 게시하든 같은 배수 지점으로 모인다.
    /// </summary>
    public override SynchronizationContext CreateCopy() => this;

    /// <summary>
    /// 쌓인 재개를 게임 스레드에서 돌린다. 처리한 건수를 돌려준다.
    /// </summary>
    internal int Drain()
    {
        // 배수는 게임 스레드의 일이다. 여기가 어긋나면 마셜링이 스스로 규약을 깬다.
        if (!Native.IsGameThread)
        {
            Native.Log(3, "[GameThread] 배수가 게임 스레드 밖에서 불렸다 — 건너뛴다.");
            return 0;
        }

        int budget = _queue.Count;
        int ran = 0;

        while (ran < budget && _queue.TryDequeue(out Continuation item))
        {
            ++ran;
            try
            {
                item.Callback(item.State);
            }
            catch (Exception ex)
            {
                // 여기까지 예외가 올라오는 것은 드물다 — 비동기 본문의 예외는 상태
                // 기계가 Task로 거둬 간다. 그래도 격리한다. 재개 하나가 나머지 재개와
                // 그 프레임을 통째로 끌고 가는 것이 이 자리에서 가장 나쁜 결과다.
                Native.Log(3, $"[GameThread] 재개 중 예외 — 나머지 재개는 계속한다.\n{ex}");
            }
        }

        return ran;
    }

    /// <summary>
    /// 남은 재개를 버린다. 어셈블리 리로드처럼 스크립트 타입 참조를 하나도 남기면
    /// 안 되는 경로에서 <see cref="ScriptRegistry.Clear"/>가 부른다.
    ///
    /// 버리는 것이 맞다 — 그 시점에는 스코프가 이미 취소됐고, 대기하던 본문을 되살려
    /// 봐야 사라진 객체를 만진다. 다만 <b>조용히</b> 버리지는 않는다. 건수가 남으면
    /// 그만큼의 <c>async</c> 상태 기계가 완료되지 않은 채 끊긴 것이고, 그것은
    /// 참조 유지(LC3)에서 다시 볼 자리다.
    /// </summary>
    internal int DropPending()
    {
        int dropped = 0;
        while (_queue.TryDequeue(out _)) ++dropped;
        return dropped;
    }
}
