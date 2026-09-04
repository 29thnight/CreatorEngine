using System.Threading;

namespace CreatorEngine;

/// <summary>
/// 컴포넌트 하나의 시뮬레이션 수명에 귀속되는 작업 모음.
///
/// Verse의 spawn/suspends를 C#으로 번안한 것이다(설계 문서 SceneGraphRedesignPlan §4
/// 트랙 L). OnBeginSimulation에서 시작한 태스크·이벤트 구독은 여기 등록해 두면
/// OnEndSimulation 직전에 <see cref="ScriptRegistry"/>가 일괄 취소한다 —
/// "구독만 있고 해지가 없는" 구조(설계 문서가 지목한 N-4)가 이 스코프 하나로 막힌다.
///
/// <see cref="Delay"/>는 관리 측 프레임 틱(<see cref="ScriptRegistry.Update"/>)에서
/// 구동되는 결정적 지연이다. <c>Task.Delay</c>(벽시계)를 쓰지 않는 이유는, 벽시계는
/// 일시정지·프레임 배속과 무관하게 흐르기 때문이다 — 여기서는 엔진 dt가 시간의
/// 유일한 정본이다. 시뮬레이션 태스크는 경계를 넘지 않고 이 클래스 안에서만
/// 산다(틱당 1회 크로싱 불변). 게임 스레드 전용이다(관리 코드 호출 규약과 같다 —
/// ClrHost.h 참고).
/// </summary>
public sealed class SimulationScope
{
    private sealed class PendingDelay
    {
        public float Remaining;
        public TaskCompletionSource<bool> Completion = null!;
    }

    private CancellationTokenSource _cts = new();
    private readonly List<PendingDelay> _pending = new();

    /// <summary>이 스코프가 살아 있는 동안 유효한 취소 토큰. Cancel 뒤에는 새 토큰으로 바뀐다.</summary>
    public CancellationToken Token => _cts.Token;

    /// <summary>
    /// seconds 뒤에 완료되는 태스크. 엔진 프레임 dt로 흐르므로 <c>await Scope.Delay(3f)</c>가
    /// <c>while(timer &gt; 3) yield</c> 폴링을 대신하는 관용구가 된다. 스코프가 먼저
    /// 취소되면(OnEndSimulation) 대기도 함께 취소된다.
    /// </summary>
    public Task Delay(float seconds)
    {
        CancellationToken token = _cts.Token;
        if (token.IsCancellationRequested) return Task.FromCanceled(token);

        var pending = new PendingDelay { Remaining = seconds, Completion = new TaskCompletionSource<bool>() };
        _pending.Add(pending);

        token.Register(() => pending.Completion.TrySetCanceled(token));
        return pending.Completion.Task;
    }

    /// <summary>
    /// (add, remove) 쌍으로 표현되는 구독에 handler를 걸고, 스코프가 취소되면 자동으로
    /// 해지한다.
    ///
    /// 사용 예: <c>Scope.Subscribe(h =&gt; target.Changed += h, h =&gt; target.Changed -= h, OnChanged);</c>
    /// </summary>
    public void Subscribe(Action<Action> add, Action<Action> remove, Action handler)
    {
        add(handler);
        RegisterCleanup(() => remove(handler));
    }

    /// <summary>인자 하나를 받는 이벤트용 오버로드.</summary>
    public void Subscribe<T>(Action<Action<T>> add, Action<Action<T>> remove, Action<T> handler)
    {
        add(handler);
        RegisterCleanup(() => remove(handler));
    }

    /// <summary>
    /// 스코프가 끝날 때 함께 실행할 정리 동작을 등록한다. Subscribe가 내부적으로 쓰는
    /// 일반형이다 — 이벤트 해지가 아닌 다른 정리(타이머 취소, 핸들 반납 등)에도 쓴다.
    /// </summary>
    public void RegisterCleanup(Action cleanup)
    {
        CancellationToken token = _cts.Token;
        if (token.IsCancellationRequested) { cleanup(); return; }
        token.Register(cleanup);
    }

    /// <summary>
    /// 매 프레임 대기열을 한 칸 흘려보낸다. <see cref="ScriptRegistry.Update"/>가 부른다.
    /// </summary>
    internal void Tick(float dt)
    {
        if (_pending.Count == 0) return;

        // 완료된 항목을 먼저 골라내고 지운다 — 완료 콜백이 같은 스코프에 새 Delay를
        // 걸 수 있어(연쇄 지연), 원본을 그대로 돌면서 지우면 순회가 흔들린다.
        for (int i = _pending.Count - 1; i >= 0; --i)
        {
            PendingDelay p = _pending[i];
            p.Remaining -= dt;
            if (p.Remaining > 0f) continue;

            _pending.RemoveAt(i);
            p.Completion.TrySetResult(true);
        }
    }

    /// <summary>
    /// 등록된 작업을 전부 취소하고 다음 시뮬레이션을 위해 토큰을 새로 연다.
    /// <see cref="ScriptRegistry"/>가 OnEndSimulation 직전에 부른다.
    /// </summary>
    internal void Cancel()
    {
        _cts.Cancel();
        _pending.Clear();
        _cts.Dispose();
        _cts = new CancellationTokenSource();
    }
}
