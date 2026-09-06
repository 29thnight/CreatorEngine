namespace CreatorEngine.Scripts;

/// <summary>
/// LC0 픽스처 — <b>배수 안에서 다시 배수를 부른다.</b>
///
/// LC5-b의 배수는 진입 시점의 건수만 처리한다. 재개된 본문이 이미 완료된 것을
/// 다시 <c>await</c>하면 그 자리에서 또 게시되는데, 그것까지 따라가면 한 프레임이
/// 끝나지 않을 수 있어서다. 그 예산이 실제로 도는지를 재는 것이 없었다 —
/// 코드에 그렇게 적어 두었을 뿐이다.
///
/// ── 왜 <c>Task.Yield</c>인가 ──
///
/// <c>Scope.Delay</c>는 시간이 흘러야 완료되므로 "이미 완료된 것을 다시 await"를
/// 만들 수 없다. <c>Task.Yield</c>는 즉시 컨텍스트로 게시된다 — 배수 안에서
/// 부르면 <b>같은 프레임의 배수 대기열에</b> 들어간다. 예산이 없으면 그 자리에서
/// 곧바로 이어져 열 번이 한 프레임에 몰리고, 예산이 있으면 프레임마다 하나씩
/// 나뉜다.
///
/// 그래서 판정 축은 <b>몇 프레임에 걸쳐 왔는가</b>다. 열 번이 다 오는 것만으로는
/// 예산의 유무를 가릴 수 없다 — 예산이 없어도 열 번은 다 온다.
///
/// ── 무한 재진입을 만들지 않는 이유 ──
///
/// 끝없이 <c>Yield</c>하는 픽스처는 예산이 없을 때 프레임을 영영 끝내지 못해
/// 하네스가 타임아웃으로 죽는다. 그러면 로그가 flush 전에 사라져 증거가 남지
/// 않는다. 열 번으로 끊으면 예산이 없어도 실행이 끝나고, 프레임 분포가 그 사실을
/// 말해 준다.
/// </summary>
public sealed partial class DrainReentryProbe : Component
{
    private const string Kind = "drain";
    private const int Rounds = 10;

    private void Mark(string point)
        => Log($"[LC0] kind={Kind} point={point} frame={FrameCount} tid={Environment.CurrentManagedThreadId}");

    public override void OnBeginSimulation()   => Mark("beginhook");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    public override async Task OnSimulate()
    {
        Mark("sim");

        for (int i = 0; i < Rounds; ++i)
        {
            await Task.Yield();
            Mark("yield");
        }

        Mark("done");
    }
}
