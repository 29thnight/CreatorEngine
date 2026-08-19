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

    public override void OnInitialized()
    {
        _awakeFrame = FrameCount;
        Log($"[Probe] Awake — {GameObject.Name} · 엔진프레임 {_awakeFrame}");
    }

    public override void OnBeginSimulation()
    {
        Log($"[Probe] Start — {GameObject.Name} · 엔진프레임 {FrameCount} (Awake는 {_awakeFrame})");
    }

    // 씬 편입/이탈 — 이 둘은 두 기제에서 온다(설계 문서 §4 트랙 L · L3 잔여).
    //   · 생성 시   : BehaviourRegistry의 드레인(관리 측 자체 큐)
    //   · 이송 시   : 네이티브 Scene::Attach/DetachGameObjectHierarchy의 직접 전달
    // 후자가 실제로 닿는지 재는 것이 이 두 로그의 목적이다 — DontDestroyOnLoad
    // 오브젝트는 파괴되지 않으므로 TearDown을 타지 않고, 통지가 없으면 스크립트는
    // 씬이 바뀐 사실을 영영 모른다.
    public override void OnAddedToScene()
    {
        Log($"[Probe] AddedToScene — {GameObject.Name} · 엔진프레임 {FrameCount}");
    }

    public override void OnRemovingFromScene()
    {
        Log($"[Probe] RemovingFromScene — {GameObject.Name} · 엔진프레임 {FrameCount}");
    }
}
