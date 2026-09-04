namespace CreatorEngine.Scripts;

/// <summary>
/// 생명주기 <b>축</b> 프로브 — 6단계가 아니라 그 둘레의 두 축을 잰다.
///
/// ── 무엇을 재는가 ──
///
/// 기존 <see cref="LifecycleProbe"/>는 훅이 <b>왔는가·순서가 맞는가</b>를 본다.
/// 이 프로브는 그 훅들이 <b>어느 모드에서</b> 왔는지, 그리고 <b>틱이 함께
/// 돌았는지</b>를 본다. 그 둘을 가르지 않으면 아래 셋이 전부 보이지 않는다:
///
///   T1 편집 모드에서 6단계가 이미 돈다.
///      EditorMain.cpp:510이 "편집 모드에서는 스크립트를 돌리지 않는다(Unity와
///      같은 규약). 붙여 둔 스크립트는 보류 큐에 쌓였다가 재생 시작 시 한꺼번에
///      Awake된다"고 적어 뒀지만, 그 return 앞의 SceneManagers-&gt;Editor()가
///      매 프레임 DrainPendingLifecycle을 돌린다(SceneManager.cpp:382). 재생
///      시작에는 State_Initialized·State_SimulationBegun이 이미 서 있어
///      <b>훅이 하나도 다시 불리지 않는다.</b>
///
///   T2 재생 정지에서 축소 삼단이 오지 않는다.
///      ScriptRegistry.Flush는 관리 틱 안에만 있고, 편집 모드에는 관리 틱이
///      없다. 정지 시퀀스에서 네이티브 축소는 DispatchLifecycle의 IsAlive
///      가드에 걸려 버려지고(Entity::Destroy가 핸들을 먼저 무효화한다),
///      폴백인 TearDown은 다음 재생 세션 첫 프레임까지 밀린다.
///
///   T3 오브젝트를 꺼도 스크립트가 계속 돈다.
///      ScriptComponent가 OnEnable/OnDisable을 override하지 않아 활성 축이
///      경계에서 끊겨 있다. Entity::SetEnabled는 전 컴포넌트로 전파되지만
///      그 전파가 관리 측 <see cref="Component.Enabled"/>에 닿지 않고, 틱
///      게이트는 그 관리 측 값만 본다.
///
/// ── 로그 한 줄 형식 ──
///
/// <code>[Axis] #서수 kind=hook|tick|mark name=이름 frame=엔진프레임 pre=n post=m</code>
///
/// 게이트(verify-lifecycle-axis.ps1)는 이 한 형식만 판다. 서수는 프로세스 안에서
/// 이 타입의 몇 번째 인스턴스인지다 — 재생 정지가 씬을 백업에서 되살리므로
/// 인스턴스가 새로 하나 더 뜬다. 그 <b>#2가 T1의 오염 없는 증거</b>다:
/// script.add는 자기가 DrainPendingLifecycle을 동기로 부르지만(#1은 그래서
/// 오염됐다), 복원분에는 그런 명령이 관여하지 않는다.
///
/// ── 왜 틱 수를 훅 로그에 함께 싣는가 ──
///
/// "편집 모드에서 훅이 왔다"와 "그때 틱도 돌았다"를 한 줄로 가르기 위해서다.
/// 두 줄로 나누면 게이트가 시간축을 스스로 짜맞춰야 하고, 그러면 프레임이
/// 빠른 헤드리스에서 판정이 흔들린다.
/// </summary>
public sealed partial class LifecycleAxisProbe : Component
{
    /// <summary>틱 로그를 몇 번째마다 남기는가. 전량을 남기면 로그가 폭주한다.</summary>
    private const int BeatEvery = 300;

    /// <summary>비활성 전이 전 예열. 초 단위 — 헤드리스 프레임 속도를 모르므로 시간축을 쓴다.</summary>
    private const float WarmupSeconds = 0.15f;

    /// <summary>비활성/재활성 구간 각각의 관측 길이.</summary>
    private const float ObserveSeconds = 0.15f;

    private static int s_created;

    // 필드 초기화자에서 증가한다 — 생성자보다 앞이라 훅이 서수 없이 찍히는 창이 없다.
    private readonly int _ordinal = ++s_created;

    private int _pre;
    private int _post;

    private void Emit(string kind, string name)
        => Log($"[Axis] #{_ordinal} kind={kind} name={name} frame={FrameCount} pre={_pre} post={_post}");

    // ── 6단계 ──

    public override void OnInitialized()       => Emit("hook", "OnInitialized");
    public override void OnAddedToScene()      => Emit("hook", "OnAddedToScene");
    public override void OnBeginSimulation()   => Emit("hook", "OnBeginSimulation");
    public override void OnEndSimulation()     => Emit("hook", "OnEndSimulation");
    public override void OnRemovingFromScene() => Emit("hook", "OnRemovingFromScene");

    /// <summary>
    /// 마지막 훅. 여기서 <b>자기 오브젝트에 닿는지</b>도 함께 남긴다.
    ///
    /// 훅이 왔다는 것만으로는 반쪽이다 — 예전에는 <c>Entity::Destroy</c>가 파괴
    /// 표시 시점에 스크립트 핸들을 무효화해서, 축소 훅이 도착할 무렵이면 이미
    /// 자기 오브젝트에 닿을 수 없었다(이름은 빈 문자열, GetComponent는 무응답).
    /// 정리 코드가 조용히 아무 일도 안 하는 모습이라 눈으로는 알아채기 어렵다.
    /// </summary>
    public override void OnUninitializing()
    {
        Emit("hook", "OnUninitializing");
        Emit("mark", 0 < Entity.Name.Length ? "ownerreachable" : "ownerlost");
    }

    // ── 활성 축 ──
    //
    // 이 둘이 T3의 판정 지점이다. 처음 이 프로브를 돌렸을 때는 로그가 한 줄도
    // 남지 않았다 — ScriptComponent가 OnEnable/OnDisable을 override하지 않아
    // Entity::SetEnabled의 컴포넌트 전파가 기반 클래스의 빈 함수에서 끝났다.
    // 배선(ScriptComponent.h · ClrHost::DispatchEnabled)이 들어온 뒤로는 양방향
    // 전이가 모두 여기로 온다.

    public override void OnEnable()  => Emit("hook", "OnEnable");
    public override void OnDisable() => Emit("hook", "OnDisable");

    // ── 틱 축 ──

    public override void PrePhysics(float tick)
    {
        ++_pre;
        if (1 == _pre) Emit("tick", "prefirst");
        else if (0 == _pre % BeatEvery) Emit("tick", "prebeat");
    }

    public override void PostPhysics(float tick)
    {
        ++_post;
        if (1 == _post) Emit("tick", "postfirst");
        else if (0 == _post % BeatEvery) Emit("tick", "postbeat");
    }

    /// <summary>
    /// 활성 전이를 스스로 몰아 T3를 재는 본문.
    ///
    /// ── 왜 틱 수가 아니라 Scope.Delay로 모는가 ──
    ///
    /// 틱 수를 세어 전이를 걸면(예: post가 60이면 끈다) <b>고친 뒤에 프로브가
    /// 멈춘다</b> — 비활성 전파가 실제로 동작하는 순간 틱이 멎어 다음 전이 조건에
    /// 영영 닿지 못한다. Scope는 Enabled와 무관하게 흐르는 것이 설계이므로
    /// (ScriptRegistry.PrePhysicsTick의 주석) 이쪽은 고침 전후 모두 진행한다.
    ///
    /// ── 왜 disabled 마크를 SetEnabled 직후에 찍는가 ──
    ///
    /// 고침이 들어오면 OnDisable 훅 줄이 <c>predisable</c>과 <c>disabled</c>
    /// 사이에 끼어 순서까지 함께 증명된다. 마크가 하나뿐이면 "불리긴 했다"만 남고
    /// 그 시점은 못 가른다.
    /// </summary>
    public override async Task OnSimulate()
    {
        Emit("mark", "simstart");
        try
        {
            await Scope.Delay(WarmupSeconds);
            Emit("mark", "predisable");

            Entity.SetEnabled(false);
            Emit("mark", "disabled");

            // 이 구간의 post 증가분이 곧 판정이다 — 꺼진 스크립트는 0이어야 한다.
            await Scope.Delay(ObserveSeconds);
            Emit("mark", "postdisable");

            Entity.SetEnabled(true);
            Emit("mark", "reenabled");

            // 되살아나는지도 함께 본다. 이 구간이 0이면 '끄는 것'이 아니라
            // '되살리지 못하는 것'이라 고침의 방향이 정반대가 된다.
            await Scope.Delay(ObserveSeconds);
            Emit("mark", "postreenable");

            // ── 역방향: 스크립트가 스스로 끈다 ──
            //
            // 위 왕복은 Entity.SetEnabled(오브젝트 전체) — 네이티브가 시작점이다.
            // 이쪽은 Component.Enabled setter가 시작점이라 경계를 반대로 건넌다
            // (관리 → Api_Script_SetEnabled → Component::SetEnabled → OnDisable →
            //  관리). 두 방향이 같은 훅 하나로 수렴하는지를 여기서 본다.
            //
            // 배선만 하고 태우지 않으면 '생산만 있고 소비가 0'인 코드가 된다.
            // 전달에 실패하면 setter가 경고를 남기고 국소 폴백으로 내려가는데,
            // 게이트가 그 경고 0건을 함께 판정한다 — 폴백으로 내려가도 훅과 틱은
            // 똑같이 보이므로, 경고를 세지 않으면 이 다리가 죽어도 초록이다.
            Enabled = false;
            Emit("mark", "selfdisabled");

            await Scope.Delay(ObserveSeconds);
            Emit("mark", "postselfdisable");

            Enabled = true;
            Emit("mark", "selfreenabled");

            await Scope.Delay(ObserveSeconds);
            Emit("mark", "postselfreenable");

            // 정지까지 대기한다 — 여기서 취소되는 것이 정상 경로다.
            await Scope.Delay(9999f);
            Emit("mark", "simend");
        }
        catch (OperationCanceledException)
        {
            Emit("mark", "simcancel");
        }
    }
}
