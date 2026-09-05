using System.Diagnostics;

namespace CreatorEngine.Scripts;

/// <summary>
/// LC6 픽스처 — <b>엔진 API 진입 검사의 프레임 비용.</b>
///
/// ── 왜 이 픽스처인가 ──
///
/// LC5-c가 엔진 API 206곳에 진입 검사(<c>Native.Entered</c>)를 넣었다. 검사 하나는
/// 정적 bool 읽기 + TLS 스레드 id 읽기 + int 비교인데, <b>그 비용이 한 번도
/// 측정된 적이 없다</b>. 206곳에 기계적으로 넣었다는 사실만으로는 "싸다"고 말할 수
/// 없다 — 프레임당 API 호출이 몇 번인지에 따라 달라지기 때문이다.
///
/// LC7-b가 <c>SimulationScope.Tick</c>에 넣은 프레임 번호 읽기도 같은 처지다.
/// 대기 목록이 비면 건너뛰도록 두었지만 그 가정도 재지 않았다.
///
/// ── 무엇을 재는가 ──
///
/// 같은 프레임 안에서 두 벌을 잰다.
///
///   guarded  진입 검사를 지나는 실제 엔진 API (<c>Native.FrameCount</c>)
///   bare     검사 없이 같은 모양의 일을 하는 관리 측 기준선
///
/// 차이가 곧 <b>검사 + 경계 크로싱</b>의 몫이다. guarded 만 재면 그 값이 검사
/// 때문인지 네이티브 호출 때문인지 가를 수 없다.
///
/// <c>FrameCount</c>를 고른 이유는 인자가 없고 부작용도 없어 <b>검사와 경계 왕복
/// 외에는 거의 아무 일도 하지 않는</b> API 라서다. Transform 읽기 같은 것을 쓰면
/// 그쪽 계산이 검사 비용을 덮는다.
///
/// ── 왜 프레임 시간이 아니라 직접 재는가 ──
///
/// 프레임 시간에서 이 몫을 빼내려면 렌더·물리의 흔들림을 넘어설 만큼 호출을
/// 늘려야 하고, 그러면 "정상 프레임의 비용"이 아니라 다른 것을 재게 된다.
/// 스톱워치로 호출 구간만 감싸면 흔들림 밖에서 호출당 비용이 바로 나온다.
///
/// ★ Release 빌드에서만 의미가 있다. Debug는 같은 조건에서 25배 느리고 규모별
///   개선 방향까지 뒤집는다.
/// </summary>
public sealed partial class EntryGuardBenchProbe : Component
{
    /// <summary>한 표본의 호출 수. 스톱워치 분해능 위로 올리되 한 프레임을 넘기지 않을 만큼.</summary>
    private const int Calls = 1_000_000;

    /// <summary>버릴 프레임 수. JIT 가 첫 호출에서 돌므로 그것을 표본에 넣지 않는다.</summary>
    private const int WarmupFrames = 30;

    /// <summary>
    /// 표본 수. 하나로는 흔들림을 못 본다.
    ///
    /// 첫 판은 표본 5 · 호출 10만이었는데 표본 흔들림(11.4~16.0 ns)이 재려는 차이
    /// (~2 ns)보다 커서 두 분포가 겹쳤다 — 아무것도 가르지 못했다. 호출을 열 배로
    /// 늘려 상대 오차를 줄이고 표본을 셋 배로 늘려 중앙값을 쓴다.
    /// </summary>
    private const int Samples = 15;

    private int _frames;
    private int _taken;

    /// <summary>합을 남겨 호출이 통째로 최적화돼 사라지지 않게 한다.</summary>
    private ulong _sink;

    public override void PostPhysics(float tick)
    {
        ++_frames;
        if (_frames <= WarmupFrames) { Warm(); return; }
        if (_taken >= Samples) return;

        ++_taken;

        // ① 검사를 지나는 실제 엔진 API.
        var sw = Stopwatch.StartNew();
        ulong sum = 0;
        for (int i = 0; i < Calls; ++i) sum += FrameCount;
        sw.Stop();
        long guarded = sw.ElapsedTicks;

        // ② 같은 모양의 관리 측 기준선. 경계도 검사도 지나지 않는다.
        sw.Restart();
        ulong bare = 0;
        for (int i = 0; i < Calls; ++i) bare += _sink;
        sw.Stop();
        long bareTicks = sw.ElapsedTicks;

        _sink = sum ^ bare;

        double toNs = 1_000_000_000.0 / Stopwatch.Frequency;
        Log($"[LC6] sample={_taken} calls={Calls} " +
            $"guarded_ns={(guarded * toNs / Calls):0.###} " +
            $"bare_ns={(bareTicks * toNs / Calls):0.###} " +
            $"delta_ns={((guarded - bareTicks) * toNs / Calls):0.###}");
    }

    /// <summary>워밍업. 표본과 같은 코드를 태워 JIT 를 끝내 둔다.</summary>
    private void Warm()
    {
        ulong sum = 0;
        for (int i = 0; i < 1000; ++i) sum += FrameCount;
        _sink ^= sum;
    }
}
