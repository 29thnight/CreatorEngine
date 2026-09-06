using System.Runtime.CompilerServices;

namespace CreatorEngine.Scripts;

/// <summary>
/// LC3 픽스처 — <b>완료된 대기가 스코프에 붙잡혀 있는가.</b>
///
/// ── 결함 ──
///
/// <c>SimulationScope.Delay</c>는 <c>token.Register(...)</c>의 반환
/// <c>CancellationTokenRegistration</c>을 보관하지도 해제하지도 않는다. 그 등록의
/// 클로저가 <c>PendingDelay</c>를 잡고, 그것이 <c>TaskCompletionSource</c>를,
/// 그것이 완료된 <c>Task</c>를 잡는다. <c>_pending</c>에서는 빠졌는데
/// <b>취소 토큰 쪽에서 여전히 도달할 수 있다.</b>
///
/// 그래서 오래 사는 스코프가 대기를 반복하면 완료된 Task와 캡처가 스코프가
/// 취소될 때까지 쌓인다. 재생 내내 사는 컴포넌트가 초당 몇 번씩 기다리면 그 수만큼
/// 그대로 남는다.
///
/// ── 관측 축을 메모리 총량으로 잡지 않는 이유 ──
///
/// <c>GC.GetTotalMemory</c>는 엔진 전체의 잡음을 함께 잰다. 몇십 KB 차이는 그
/// 잡음에 묻히고, 묻히지 않게 하려고 규모를 키우면 이번엔 시나리오가 길어진다.
///
/// 대신 <b>도달 가능성</b>을 직접 잰다. 완료된 Task마다 약한 참조를 하나 남기고,
/// 강한 참조를 모두 버린 뒤 GC를 돌려 <b>몇 개가 살아남는지</b> 센다. 이것은
/// 계획서의 완료 조건 문장 그대로다 — "스코프를 살려 둔 채 완료 Task의 외부
/// 참조를 제거하면 회수된다".
///
/// 잡음이 0이라는 것이 이 축의 값이다. 살아남은 개수가 곧 누수 개수다.
///
/// ── 강한 참조를 확실히 버리는 법 ──
///
/// Task를 지역 변수에 담았다가 <c>null</c>을 대입하는 것으로는 부족하다. Debug
/// 빌드는 지역을 스코프 끝까지 살려 두고, 비동기 본문이면 상태 기계 필드에 남는다.
/// 그래서 생성과 약한 참조 만들기를 <b>인라인하지 않는 별도 메서드</b>에 넣는다 —
/// 그 프레임이 돌아오면 강한 참조가 남을 자리가 없다.
///
/// 같은 이유로 <c>await</c>하지 않는다. 기다리면 그 Task가 상태 기계에 잡힌다.
/// 기다리지 않아도 <c>SimulationScope.Tick</c>이 시간을 흘려 완료시키므로 재려는
/// 것에는 영향이 없다.
/// </summary>
public sealed partial class DelayRetentionProbe : Component
{
    private const int Waits = 50;

    private void Mark(string point)
        => Log($"[LC3] point={point} frame={FrameCount}");

    public override void OnBeginSimulation()   => Mark("begin");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    private readonly List<WeakReference> _weak = new(Waits);
    private int _ticks;
    private int _phase;

    /// <summary>
    /// 대기 하나를 시작하고 그 Task에 대한 약한 참조만 돌려준다. 인라인을 막아
    /// 호출자 프레임에 Task가 남지 않게 한다.
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private WeakReference StartOne() => new(Scope.Delay(0.05f));

    public override void PostPhysics(float tick)
    {
        ++_ticks;

        switch (_phase)
        {
            // ① 몇 프레임 정상으로 돌린 뒤 시작한다 — 편입 직후 프레임과 겹치면
            //    무엇이 원인인지 흐려진다.
            case 0 when _ticks >= 5:
                for (int i = 0; i < Waits; ++i) _weak.Add(StartOne());
                Log($"[LC3] point=created frame={FrameCount} alive=0 of={_weak.Count}");
                _phase = 1;
                break;

            // ② 0.05초짜리 대기가 전부 만기될 때까지 둔다. 같은 프레임에 만들었으니
            //    같은 프레임에 함께 완료된다.
            case 1 when _ticks >= 30:
                // 완료된 Task를 붙잡는 것이 남아 있는지만 본다. 두 번 돌리는 이유는
                // 파이널라이저를 거쳐야 풀리는 사슬을 놓치지 않기 위해서다.
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();

                int alive = 0;
                foreach (WeakReference w in _weak) { if (w.IsAlive) ++alive; }

                // 스코프는 아직 살아 있다 — 이 컴포넌트가 재생 중이므로. 그 상태에서
                // 살아남은 개수가 곧 스코프가 붙잡고 있는 완료 Task의 수다.
                Log($"[LC3] point=counted frame={FrameCount} alive={alive} of={_weak.Count}");
                _phase = 2;
                break;
        }
    }
}
