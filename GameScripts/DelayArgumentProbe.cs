namespace CreatorEngine.Scripts;

/// <summary>
/// LC7 픽스처 — <b>대기 인자의 지원 의미</b>.
///
/// ── 왜 이 시나리오인가 ──
///
/// <c>Scope.Delay(seconds)</c>는 저작 표면의 관용구인데, 어떤 인자가 무엇을 뜻하는지
/// 계약이 적힌 적이 없다. 0은? 음수는? 계산이 어긋나 <c>NaN</c>이 들어오면?
/// 무한대는? 그리고 연쇄로 걸면 같은 프레임에서 무한히 재개되지는 않는가(LC4가
/// 여기로 넘긴 항목).
///
/// 코드를 읽어 세운 예상은 이렇다 — <c>Remaining -= dt</c> 뒤 <c>Remaining &gt; 0f</c>
/// 로 판정하므로 0·음수는 다음 틱에 완료되고, 무한대는 영원히 완료되지 않으며,
/// <c>NaN</c>은 <c>NaN &gt; 0f</c> 가 <b>거짓</b>이라 0과 똑같이 완료된다. 마지막
/// 항목이 문제다: 저작자의 계산 실수가 "한 프레임 대기"로 조용히 삼켜진다.
///
/// 예상은 예상이므로 재고 나서 판정한다.
///
/// ── 어떻게 재는가 ──
///
/// 대기 전후의 <see cref="Component.FrameCount"/>를 함께 남긴다. 차이가 곧 그
/// 대기가 소비한 프레임 수다. "완료됐다/안 됐다"만으로는 0과 음수를 가를 수 없고,
/// 연쇄가 프레임당 한 칸씩 나아가는지도 볼 수 없다.
///
/// <c>NaN</c>은 거부(예외)와 완료를 <b>둘 다</b> 표지로 남긴다. 거부가 옳은 계약이라
/// 판단해 고치더라도, 고치기 전 상태를 이 픽스처가 그대로 관측해야 게이트가
/// RED에서 GREEN으로 넘어가는 것을 보일 수 있다.
/// </summary>
public sealed partial class DelayArgumentProbe : Component
{
    private const int ChainSteps = 5;

    private void Mark(string text) => Log($"[LC7b] {text}");

    public override async Task OnSimulate()
    {
        // ① 0초 — "다음 프레임까지"의 관용구로 쓰이는 값이다.
        ulong start = FrameCount;
        await Scope.Delay(0f);
        Mark($"case=zero start={start} end={FrameCount}");

        // ② 음수 — 이미 지난 시각. 0과 같아야 하는지, 다르게 취급하는지.
        start = FrameCount;
        await Scope.Delay(-1f);
        Mark($"case=negative start={start} end={FrameCount}");

        // ③ NaN — 저작 계산이 어긋났을 때 들어오는 값.
        start = FrameCount;
        try
        {
            await Scope.Delay(float.NaN);
            Mark($"case=nan outcome=completed start={start} end={FrameCount}");
        }
        catch (ArgumentException)
        {
            Mark($"case=nan outcome=rejected start={start} end={FrameCount}");
        }

        // ④ 연쇄 — 완료 continuation이 같은 스코프에 새 대기를 건다. 같은 프레임에서
        //    계속 이어지면 프레임이 하나도 흐르지 않고 무한 재개가 된다.
        start = FrameCount;
        for (int i = 0; i < ChainSteps; ++i) await Scope.Delay(0f);
        Mark($"case=chain start={start} end={FrameCount} steps={ChainSteps}");

        // ⑤ 무한대 — 스스로 완료되지 않고 스코프 취소로만 풀려야 한다. 이 대기는
        //    정지가 끊으므로 아래 표지 중 하나만 남는다.
        start = FrameCount;
        try
        {
            await Scope.Delay(float.PositiveInfinity);
            Mark($"case=infinite outcome=completed start={start} end={FrameCount}");
        }
        catch (OperationCanceledException)
        {
            Mark($"case=infinite outcome=cancelled start={start} end={FrameCount}");
        }
    }
}
