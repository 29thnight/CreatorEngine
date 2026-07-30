namespace CreatorEngine.Scripts;

/// <summary>
/// 라이프사이클 호출 시점을 관측하는 진단용 스크립트.
///
/// 프레임 번호는 엔진에서 직접 받는다. 스크립트가 세는 값을 쓰면 TickAwake가
/// TickUpdate보다 먼저 도는 탓에 "같은 프레임"으로 잘못 보인다.
/// </summary>
public sealed partial class LifecycleProbe : Behaviour
{
    private ulong _awakeFrame;

    public override void Awake()
    {
        _awakeFrame = FrameCount;
        Log($"[Probe] Awake — {GameObject.Name} · 엔진프레임 {_awakeFrame}");
    }

    public override void Start()
    {
        Log($"[Probe] Start — {GameObject.Name} · 엔진프레임 {FrameCount} (Awake는 {_awakeFrame})");
    }
}
