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
        Log($"[Probe] Awake — {Entity.Name} · 엔진프레임 {_awakeFrame}");
    }

    public override void OnBeginSimulation()
    {
        Log($"[Probe] Start — {Entity.Name} · 엔진프레임 {FrameCount} (Awake는 {_awakeFrame})");
    }

    // 씬 편입/이탈 — 이 둘은 두 기제에서 온다(설계 문서 §4 트랙 L · L3 잔여).
    //   · 생성 시   : BehaviourRegistry의 드레인(관리 측 자체 큐)
    //   · 이송 시   : 네이티브 Scene::DetachEntityHierarchy/
    //                 AttachExistingEntityHierarchy의 직접 전달
    // 후자가 실제로 닿는지 재는 것이 이 두 로그의 목적이다 — DontDestroyOnLoad
    // 오브젝트는 파괴되지 않으므로 TearDown을 타지 않고, 통지가 없으면 스크립트는
    // 씬이 바뀐 사실을 영영 모른다.
    public override void OnAddedToScene()
    {
        Log($"[Probe] AddedToScene — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public override void OnRemovingFromScene()
    {
        Log($"[Probe] RemovingFromScene — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    // 나머지 축도 전부 남긴다 — 관리 측 생명주기의 드라이버를 네이티브로 옮기는
    // 동안(설계 문서 §4 트랙 L · L3 잔여 2단계) **순서가 보존됐는지**를 재는 자가
    // 이 로그다. 네이티브 기준선(생명주기 200사건)은 네이티브 컴포넌트만 담아
    // 관리 측 훅 순서를 말하지 못한다.
    public override void OnEndSimulation()
    {
        Log($"[Probe] EndSimulation — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public override void OnUninitializing()
    {
        Log($"[Probe] Uninitializing — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public override void OnEnable()
    {
        Log($"[Probe] Enable — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    public override void OnDisable()
    {
        Log($"[Probe] Disable — {Entity.Name} · 엔진프레임 {FrameCount}");
    }

    // 시뮬레이션 본문(설계 문서 §4 트랙 L5). 이 로그 셋이 재는 것:
    //   ① 본문이 OnBeginSimulation 직후 시작되는가
    //   ② Scope.Delay가 엔진 dt로 흘러 재개되는가(벽시계가 아니다)
    //   ③ 엔티티 제거에서 취소가 **OnEndSimulation보다 먼저** 오는가
    public override async Task OnSimulate()
    {
        Log($"[Probe] SimulateStart — {Entity.Name} · 엔진프레임 {FrameCount}");
        try
        {
            await Scope.Delay(0.2f);
            Log($"[Probe] SimulateResume — {Entity.Name} · 엔진프레임 {FrameCount}");

            // 제거될 때까지 대기한다 — 여기서 취소되는 것이 정상 경로다.
            await Scope.Delay(9999f);
            Log($"[Probe] SimulateEnd — {Entity.Name} (여기 오면 취소가 안 걸린 것이다)");
        }
        catch (OperationCanceledException)
        {
            Log($"[Probe] SimulateCancel — {Entity.Name} · 엔진프레임 {FrameCount}");
        }
    }
}
