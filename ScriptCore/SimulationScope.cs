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

        /// <summary>
        /// 이 대기의 취소 등록 (LC3 · 2026-09-05).
        ///
        /// 예전에는 <c>token.Register(...)</c>의 반환을 <b>버렸다</b>. 그러면 그
        /// 등록의 클로저가 이 객체를 잡고, 이 객체가 <see cref="Completion"/>을,
        /// 그것이 완료된 <c>Task</c>를 잡는다 — <c>_pending</c>에서 빠진 뒤에도
        /// 취소 토큰 쪽에서 계속 도달할 수 있다.
        ///
        /// 실측(<c>verify-lifecycle-retention</c>): 완료된 대기 50개 중 50개가
        /// 스코프가 살아 있는 동안 회수되지 않았다. 재생 내내 사는 컴포넌트가
        /// 초당 몇 번씩 기다리면 그 수만큼 그대로 쌓인다.
        /// </summary>
        public CancellationTokenRegistration Registration;

        /// <summary>
        /// 이 대기가 등록된 엔진 프레임 (LC7-b · 2026-09-05).
        ///
        /// <see cref="SimulationScope.Tick"/>이 이 프레임에는 만기 판정을 하지 않는다.
        /// 없으면 <c>Delay(0f)</c>가 등록 위치에 따라 0 프레임이 되기도, 1 프레임이
        /// 되기도 한다 — 갈림은 등록이 그 프레임의 <c>Tick</c>보다 앞이냐 뒤냐이고,
        /// 그것은 네이티브가 훅을 프레임의 어느 지점에서 부르는지에 달렸다.
        /// 저작자가 볼 수도 제어할 수도 없는 값이라 계약이 될 수 없다.
        ///
        /// 실측(착수 시점): OnSimulate 첫 줄의 <c>Delay(0f)</c>는 159 → 159,
        /// 그 다음 줄의 것은 159 → 160.
        /// </summary>
        public ulong CreatedFrame;
    }

    private CancellationTokenSource _cts = new();
    private readonly List<PendingDelay> _pending = new();

    /// <summary>이 스코프가 살아 있는 동안 유효한 취소 토큰. Cancel 뒤에는 새 토큰으로 바뀐다.</summary>
    public CancellationToken Token => _cts.Token;

    /// <summary>
    /// seconds 뒤에 완료되는 태스크. 엔진 프레임 dt로 흐르므로 <c>await Scope.Delay(3f)</c>가
    /// <c>while(timer &gt; 3) yield</c> 폴링을 대신하는 관용구가 된다. 스코프가 먼저
    /// 취소되면(OnEndSimulation) 대기도 함께 취소된다.
    ///
    /// ── 인자의 뜻 (LC7-b) ──
    ///
    /// <list type="bullet">
    /// <item><b>0 이하</b> — 다음 프레임까지. 음수는 0과 같다(이미 지난 시각에 따로
    /// 뜻을 주지 않는다). <c>await Scope.Delay(0f)</c>가 "한 프레임 양보"의
    /// 관용구다.</item>
    /// <item><b>양의 무한대</b> — 스스로 완료되지 않는다. 스코프 취소로만 풀리므로
    /// "끝날 때까지 매달려 있기"의 관용구가 된다.</item>
    /// <item><b>NaN</b> — 거부한다. 뜻을 줄 수 있는 값이 아니고, 무엇보다 만기
    /// 판정(<c>Remaining &gt; 0f</c>)이 NaN에 거짓이라 <b>0초와 구별되지 않는다</b>.
    /// 거부하지 않으면 저작자의 계산 실수가 "한 프레임 대기"로 삼켜져 아무 신호도
    /// 남지 않는다.</item>
    /// </list>
    ///
    /// 어느 인자든 <b>최소 한 프레임</b>을 쓴다 — 등록한 프레임에는 만기 판정을
    /// 하지 않는다(<see cref="PendingDelay.CreatedFrame"/>).
    /// </summary>
    /// <exception cref="ArgumentException">seconds가 NaN일 때.</exception>
    public Task Delay(float seconds)
    {
        // 만기 판정보다 앞에서 걸러야 한다. 뒤로 미루면 NaN은 0초와 같은 길로 가고
        // 그 길에는 이 값을 되돌아볼 자리가 없다.
        if (float.IsNaN(seconds))
        {
            throw new ArgumentException(
                "Scope.Delay(NaN)은 뜻을 줄 수 없다 — 만기 판정에서 0초와 구별되지 않아 " +
                "계산 실수가 '한 프레임 대기'로 조용히 삼켜진다. 대기 시간을 계산한 " +
                "식을 확인하라(0으로 나눔, 미초기화 값).",
                nameof(seconds));
        }

        CancellationToken token = _cts.Token;
        if (token.IsCancellationRequested) return Task.FromCanceled(token);

        var pending = new PendingDelay
        {
            Remaining = seconds,
            Completion = new TaskCompletionSource<bool>(),
            CreatedFrame = Native.FrameCount,
        };
        _pending.Add(pending);

        // 등록을 레코드가 소유한다 — 정상 완료 시 Tick이 놓는다(LC3).
        // 위에서 이미 취소 여부를 확인했으므로 이 Register가 그 자리에서 콜백을
        // 실행하는 일은 없다. 만에 하나 그렇더라도 필드에는 기본값이 남고,
        // 기본 등록의 Dispose는 아무 일도 하지 않는다.
        pending.Registration = token.Register(() => pending.Completion.TrySetCanceled(token));
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

        // 등록 프레임을 가리기 위한 값. 대기가 없으면 여기까지 오지 않으므로
        // 대기를 쓰지 않는 스코프에는 이 읽기 비용이 붙지 않는다.
        ulong frame = Native.FrameCount;

        // 완료된 항목을 먼저 골라내고 지운다 — 완료 콜백이 같은 스코프에 새 Delay를
        // 걸 수 있어(연쇄 지연), 원본을 그대로 돌면서 지우면 순회가 흔들린다.
        for (int i = _pending.Count - 1; i >= 0; --i)
        {
            PendingDelay p = _pending[i];

            // 등록된 프레임에는 만기를 판정하지도, 시간을 흘리지도 않는다(LC7-b).
            // dt는 "등록 이후 흐른 시간"이어야 하는데 이 프레임의 dt는 등록보다
            // 앞선 구간까지 담고 있다. 이 한 줄이 없으면 Delay(0f)의 길이가 등록이
            // 이 Tick보다 앞이냐 뒤냐에 따라 0 프레임이 되기도 1 프레임이 되기도 한다.
            if (p.CreatedFrame == frame) continue;

            p.Remaining -= dt;
            if (p.Remaining > 0f) continue;

            _pending.RemoveAt(i);

            // 등록을 **완료보다 먼저** 놓는다(LC3). 순서가 중요하다 — 완료가
            // 먼저면 그 자리에서 이어지는 continuation이 도는 동안 등록이 아직
            // 토큰에 매달려 있고, 그 사이에 취소가 오면 이미 결정된 대기에
            // TrySetCanceled가 한 번 더 날아간다(결과는 무시되지만 하지 않을 일이다).
            //
            // 자기 콜백 안에서 부르는 Dispose는 교착하지 않는다 — 다른 스레드가
            // 실행 중일 때만 기다리는데, 이 목록은 게임 스레드 전용이다.
            p.Registration.Dispose();
            p.Completion.TrySetResult(true);
        }
    }

    /// <summary>
    /// 등록된 작업을 전부 취소하고 다음 시뮬레이션을 위해 토큰을 새로 연다.
    /// <see cref="ScriptRegistry"/>가 OnEndSimulation 직전에 부른다.
    /// </summary>
    internal void Cancel()
    {
        // 네 가지 일을 갈라 둔다(LC2 · 2026-09-05) — ① 취소 통지, ② 대기 목록 해제,
        // ③ 토큰 폐기, ④ 새 토큰. 예전에는 넷이 한 줄씩 이어져 있었고 ①이 던지면
        // 나머지 셋이 통째로 날아갔다.
        //
        // ①은 사용자 코드를 부른다 — RegisterCleanup·Subscribe로 등록한 정리
        // 콜백이 여기서 돈다. 그래서 던지는 것이 정상 범위의 사건이고, 그것이
        // ②③④를 막으면 스코프가 취소 상태로 굳은 채 목록도 안 비워진다.
        //
        // 더 나쁜 것은 호출자였다. ScriptRegistry의 축소 경로는
        // `Scope.Cancel(); Invoke(OnEndSimulation);` 순인데, ①의 예외가 그
        // 경계를 넘어 **OnEndSimulation을 통째로 건너뛰게** 했다(실측: 축소
        // 삼단 중 가운데만 빠졌다).
        //
        // 호출자 쪽에 try/catch를 덧대지 않는다. 이 함수가 던지지 않는 것이
        // 계약이고, 그 계약이 깨지면 게이트가 붉어져야 한다 — 감싸 두면 다음
        // 회귀가 조용히 지나간다.
        CancellationTokenSource old = _cts;

        // ① 취소 통지. 인자 없는 Cancel은 throwOnFirstException:false라 등록된
        //    콜백을 **전부** 돌린 뒤 AggregateException으로 모아 던진다 — 형제
        //    콜백은 이미 보호되고 있었다. 여기서 잡는 것은 그 다음을 잇기 위해서다.
        try
        {
            old.Cancel();
        }
        catch (AggregateException aggregate)
        {
            // 최초 원인을 삼키지 않는다. 콜백마다 한 줄씩 남겨 어느 것이
            // 실패했는지 스택으로 짚을 수 있게 한다(게이트가 이 건수를 센다 —
            // 삼켜도 격리는 되므로, 보고를 세지 않으면 삼킴이 초록으로 지나간다).
            foreach (Exception inner in aggregate.InnerExceptions)
            {
                Native.Log(3, $"[SimulationScope] 정리 콜백 예외 — 나머지 정리는 계속한다.\n{inner}");
            }
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[SimulationScope] 취소 통지 예외 — 나머지 정리는 계속한다.\n{ex}");
        }

        // ② 대기 목록 해제. 통지가 어떻게 끝났든 반드시 비운다.
        _pending.Clear();

        // ③ 토큰 폐기. Dispose는 등록 해제를 기다릴 수 있어 여기서도 막힐 수 있다.
        try
        {
            old.Dispose();
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[SimulationScope] 토큰 폐기 예외 — 새 토큰으로 계속한다.\n{ex}");
        }

        // ④ 다음 시뮬레이션을 위한 새 토큰. 이것이 서지 않으면 재사용되는
        //    인스턴스(DDOL 이송·재생 재시작)가 영영 취소 상태로 남는다.
        _cts = new CancellationTokenSource();
    }
}
