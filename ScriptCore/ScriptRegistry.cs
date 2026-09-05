namespace CreatorEngine;

/// <summary>
/// 살아 있는 스크립트를 모아 두고 틱마다 순회한다.
///
/// 경계는 틱당 한 번만 넘고 여기서부터는 전부 관리 영역이다(설계 문서 02절).
/// 순회 도중 스크립트가 추가·삭제될 수 있으므로 보류 큐를 두고 틱 경계에서 반영한다 —
/// 네이티브의 <c>AllDestroyMark</c> 지연 파괴와 같은 철학이다.
/// </summary>
internal static class ScriptRegistry
{
    private static readonly List<Component> _active = new();
    private static readonly List<Component> _pendingAdd = new();
    private static readonly List<Component> _pendingRemove = new();

    /// <summary>
    /// 오브젝트별 스크립트 목록. GetComponent가 경계를 넘지 않고 답하기 위한 색인이다.
    ///
    /// 실측에서 GetComponent 계열이 734회로 1위인데, 그 상당수가 스크립트끼리의 참조다.
    /// 이 색인이 있으면 그 호출들이 전부 관리 영역에서 끝난다(설계 문서 03.1 · 11.2).
    /// 보류 큐를 거치지 않고 즉시 갱신하는 이유는, 스폰 직후 Awake 안에서 바로
    /// GetComponent를 부르는 패턴이 흔하기 때문이다.
    /// </summary>
    private static readonly Dictionary<ObjectHandle, List<Component>> _byObject = new();

    // 앞쪽 세 단계(OnInitialized·OnAddedToScene·OnBeginSimulation)의 큐는 은퇴했다
    // (설계 문서 §4 트랙 L · L3 잔여 2단계). 이제 네이티브 ScriptComponent의 같은
    // 이름 훅이 DispatchLifecycle로 직접 전달한다 — 드라이버가 하나면 "관리 큐가
    // 부르는 것"과 "네이티브가 부르는 것"이 어긋날 수 없다.
    //
    // 뒤쪽 세 단계는 아직 TearDown이 부른다. 고아 청소(SweepOrphans)와 어셈블리
    // 리로드(Clear)는 **구동할 네이티브 컴포넌트가 없는** 경로라 관리 측 발화가
    // 남아야 하고, 그것과 네이티브 구동이 겹치지 않으려면 인스턴스별 '전달됨'
    // 상태가 선행이다.

    public static int ActiveCount => _active.Count;

    public static void Add(Component behaviour)
    {
        // _pendingAdd는 **틱 멤버십**이다(_active 편입) — 생명주기 단계와 무관하며
        // "경계는 틱당 1회"의 근거라 그대로 둔다.
        _pendingAdd.Add(behaviour);

        ObjectHandle owner = behaviour.Entity.Handle;
        if (!_byObject.TryGetValue(owner, out var list))
        {
            list = new List<Component>(2);
            _byObject[owner] = list;
        }
        list.Add(behaviour);
    }

    public static void Remove(Component behaviour)
    {
        behaviour.MarkDestroyed();
        _pendingRemove.Add(behaviour);

        ObjectHandle owner = behaviour.Entity.Handle;
        if (_byObject.TryGetValue(owner, out var list))
        {
            list.Remove(behaviour);
            if (list.Count == 0) _byObject.Remove(owner);
        }
    }

    /// <summary>
    /// 오브젝트에 붙은 T 타입 컴포넌트 하나. 없으면 null.
    /// GetComponent가 Component 제약으로 열려 있어 여기도 같은 제약을 받는다 —
    /// 관리 스크립트만 이 색인에 들어 있으므로 실제로 걸리는 것은 Component 파생뿐이다.
    /// </summary>
    public static T? FindComponent<T>(ObjectHandle owner) where T : Component
    {
        if (!_byObject.TryGetValue(owner, out var list)) return null;

        for (int i = 0; i < list.Count; ++i)
        {
            // 살아 있는지는 Component 쪽에서 본다 — T는 Component까지만 보장되기 때문이다.
            Component entry = list[i];
            if (entry.IsAlive && entry is T match) return match;
        }
        return null;
    }

    /// <summary>오브젝트에 붙은 T 타입 스크립트 전부.</summary>
    public static List<T> FindAll<T>(ObjectHandle owner) where T : Component
    {
        var result = new List<T>();
        if (!_byObject.TryGetValue(owner, out var list)) return result;

        for (int i = 0; i < list.Count; ++i)
        {
            if (list[i] is T match && match.IsAlive) result.Add(match);
        }
        return result;
    }

    /// <summary>보류 중인 추가·삭제를 반영한다. 반드시 순회 밖에서만 부른다.</summary>
    private static void Flush()
    {
        if (_pendingAdd.Count > 0)
        {
            _active.AddRange(_pendingAdd);
            _pendingAdd.Clear();
        }

        if (_pendingRemove.Count > 0)
        {
            foreach (var b in _pendingRemove)
            {
                _active.Remove(b);

                // Initialized 없이 Uninitializing 없음(설계 문서 §4 트랙 L, Component.cs
                // IsInitialized 참고). OnInitialized에서 잡은 것을 OnUninitializing에서
                // 놓는 것이 흔한 형태라, 짝이 맞지 않으면 초기화하지 않은 상태를
                // 정리하려 든다.
                if (b.InitializeSucceeded) TearDown(b);
            }
            _pendingRemove.Clear();
        }
    }

    /// <summary>
    /// 실제 파괴(또는 어셈블리 리로드로 인한 정리) 직전에 부른다.
    ///
    /// 네이티브 Scene::FlushPendingDestroy와 같은 순서(OnEndSimulation→
    /// OnRemovingFromScene→OnUninitializing)다. 지금 구조에서는 시뮬레이션 종료가
    /// 파괴와 분리되지 않는다 — 재생 종료는 백업에서 씬을 되살리는 방식이라
    /// DontDestroyOnLoad를 뺀 전 오브젝트가 이 경로로 파괴된다(설계 문서 §4 트랙 L1) —
    /// 그래서 셋을 같은 자리에서 순서대로 발화한다.
    ///
    /// Scope 취소를 OnEndSimulation보다 먼저 두는 이유는, 대기 중이던 태스크가 사용자의
    /// OnEndSimulation 코드보다 먼저 취소돼야 "구독만 있고 해지가 없는" 상태가 한 프레임도
    /// 남지 않기 때문이다.
    /// </summary>
    private static void TearDown(Component b)
    {
        // 네이티브가 이미 축소를 전달했으면 여기서 또 부르지 않는다(설계 문서 §4
        // 트랙 L · L3 완결). 살아 있는 컴포넌트의 파괴는 전부 네이티브
        // Scene::FlushPendingDestroy(또는 PrefabUtility의 즉시 소멸)를 지나므로
        // 정상 경로에서는 이 가드가 항상 참이다.
        //
        // 거짓인 경우가 이 함수가 남아 있는 이유다:
        //   · SweepOrphans — 소유자가 이미 사라져 구동할 네이티브 컴포넌트가 없다.
        //   · Clear(어셈블리 리로드) — 살아 있는 것까지 끊어야 한다.
        if (b.TeardownDelivered) return;

        b.Scope.Cancel();

        // 폴백 경로도 짝 규칙을 지킨다(LC1) — 네이티브 구동과 다른 계약을 쓰면
        // "고아 청소로 사라진 인스턴스만 정리 훅을 더 받는" 비대칭이 생긴다.
        if (b.BeginSucceeded) Invoke(b, static x => x.OnEndSimulation(), nameof(Component.OnEndSimulation));
        if (b.EnterSucceeded) Invoke(b, static x => x.OnRemovingFromScene(), nameof(Component.OnRemovingFromScene));
        Invoke(b, static x => x.OnUninitializing(), nameof(Component.OnUninitializing));
    }

    /// <summary>
    /// 네이티브 6단계 축의 단계 번호. ScriptBinder/ScriptLifecyclePhase.h와 **값이 같아야
    /// 한다** — 경계를 넘는 것은 int 하나라 컴파일러가 불일치를 잡아 주지 않는다.
    /// </summary>
    public enum LifecyclePhase
    {
        OnInitialized       = 0,
        OnAddedToScene      = 1,
        OnBeginSimulation   = 2,
        OnEndSimulation     = 3,
        OnRemovingFromScene = 4,
        OnUninitializing    = 5,
    }

    /// <summary>
    /// 네이티브가 단계 하나를 이 인스턴스에 직접 전달한다(설계 문서 §4 트랙 L · L3 잔여).
    ///
    /// 지금 이 경로로 오는 것은 DontDestroyOnLoad 이송의 씬 편입/이탈 둘뿐이다.
    /// 앞쪽 세 단계(OnInitialized·OnAddedToScene·OnBeginSimulation)와 DDOL 이송의
    /// OnRemovingFromScene이 이 경로로 온다. 뒤쪽 둘(OnEndSimulation·
    /// OnUninitializing)은 아직 TearDown이 부른다 — 고아 청소와 어셈블리 리로드는
    /// 구동할 네이티브 컴포넌트가 없는 경로라, 그 발화와 네이티브 구동이 겹치지
    /// 않으려면 인스턴스별 '전달됨' 상태가 선행이다.
    ///
    /// 여기서 Invoke를 지나는 것이 중요하다 — 스크립트 예외가 이송 경로를 타고
    /// 네이티브로 올라가면 씬 전환 한복판에서 터진다.
    /// </summary>
    public static bool DispatchLifecycle(int instanceId, int phase)
    {
        var b = ScriptFactory.Find(instanceId);
        if (b is null) return false;

        // ★ 축소 삼단은 생존을 묻지 않는다(2026-09-05).
        //
        // 예전에는 여기서 무조건 IsAlive를 봤는데, 그 판정은 소유자의 파괴 표시를
        // 본다(Api_Entity_IsAlive). 그런데 축소는 **정의상 죽는 중에** 오고,
        // 파괴 경로에서 그것을 부르는 Scene::FlushPendingDestroy는 표시가 선 뒤에
        // 돈다 — 즉 오브젝트 파괴에서는 세 단계가 전부 이 문턱에서 버려졌다.
        //
        // 그 결과 축소가 관리 측 폴백(TearDown)으로만 왔고, 그 폴백은 관리 틱 안의
        // Flush에만 있어 편집 모드에서는 돌지 않았다. 실측: 재생을 정지한 f964에
        // 와야 할 훅이 프로세스 종료의 f1266에 왔다(302프레임 뒤).
        //
        // 앞쪽 세 단계는 반대다 — 죽어가는 오브젝트를 초기화하면 안 되므로
        // 생존을 계속 묻는다.
        if ((LifecyclePhase)phase < LifecyclePhase.OnEndSimulation && !b.IsAlive) return false;

        // OnInitialized는 그 가드 **앞**이다 — 이 단계가 곧 IsInitialized를 세운다.
        if ((LifecyclePhase)phase == LifecyclePhase.OnInitialized)
        {
            if (b.IsInitialized) return false;   // 두 번 초기화하지 않는다
            b.MarkInitialized();
            if (Invoke(b, static x => x.OnInitialized(), nameof(Component.OnInitialized)))
            {
                b.MarkInitializeSucceeded();
            }
            return true;
        }

        // 초기화가 **성공하지** 못했으면 어떤 단계도 흘리지 않는다(LC1).
        //
        // 예전에는 IsInitialized(불렸는가)로 재서, 던진 초기화도 "초기화됐다"로
        // 남아 이후 단계가 성공한 초기화를 전제로 진행했다. 축소 삼단까지 그대로
        // 전달돼, 아무것도 잡지 못한 인스턴스가 정리 코드를 돌렸다.
        //
        // Component.IsInitialized 주석이 규정한 "짝이 맞지 않으면 스크립트가
        // 초기화하지 않은 것을 정리하려 든다"가 원래 이 축을 뜻한 것이다.
        if (!b.InitializeSucceeded) return false;

        switch ((LifecyclePhase)phase)
        {
            case LifecyclePhase.OnAddedToScene:
                // 실패하면 짝(OnRemovingFromScene)도 뒤에 오지 않는다(LC1).
                if (!Invoke(b, static x => x.OnAddedToScene(), nameof(Component.OnAddedToScene)))
                {
                    return true;
                }
                b.MarkEnterSucceeded();

                // 최초 진입에서만 OnEnable을 이어 붙인다. 활성 축은 6단계와 직교라
                // 네이티브 단계로 오지 않는데, 예전 드레인이 이 자리에서
                // (OnAddedToScene 직후) 불러 왔으므로 그 순서를 보존한다. 이송
                // 재부착은 '최초'가 아니므로 다시 불리지 않는다.
                if (!b.EnterDelivered)
                {
                    b.MarkEnterDelivered();
                    if (b.Enabled) Invoke(b, static x => x.OnEnable(), nameof(Component.OnEnable));
                }
                return true;
            case LifecyclePhase.OnRemovingFromScene:
                // 이송에서도 파괴에서도 온다. 이송은 여러 번 정상 발화하므로 여기서
                // 막지 않는다 — 파괴 경로의 중복은 TeardownDelivered가 TearDown 쪽에서 가른다.
                //
                // 짝(OnAddedToScene)이 던졌으면 이쪽도 부르지 않는다(LC1).
                if (!b.EnterSucceeded) return true;
                Invoke(b, static x => x.OnRemovingFromScene(), nameof(Component.OnRemovingFromScene));
                return true;

            case LifecyclePhase.OnEndSimulation:
                // 축소의 시작이다 — 여기서 묶음 표시를 세운다.
                b.MarkTeardownDelivered();

                // 축소는 비활성으로 시작한다(2026-09-05). Unity가 파괴에서
                // OnDisable → OnDestroy를 보장하는 것과 같은 계약이고,
                // OnEnable/OnDisable에 구독을 거는 흔한 형태가 파괴에서도
                // 짝이 맞으려면 이것이 있어야 한다.
                //
                // 어셈블리 리로드 경로(Clear)는 예전부터 이것을 하고 있었다 —
                // 즉 이 줄이 없으면 **같은 최종 정리가 경로마다 다르다.**
                // 실측으로 드러났다: 재생 정지의 축소에는 OnDisable이 없고
                // 종료의 축소에는 있었다.
                //
                // 이미 꺼진 스크립트에는 발화하지 않는다(전이가 아니므로) —
                // 그것도 Unity와 같다. 네이티브 계층에는 대응 배선을 두지
                // 않는다: FlushPendingDestroy에서 SetEnabled를 부르면 파괴 중에
                // 렌더 프록시 dirty가 발행되고, 애초에 OnDisable을 구현한
                // 네이티브 컴포넌트는 0개라 관측 가능한 대칭이 없다.
                ApplyEnabled(b, false);

                // ★ 취소가 OnEndSimulation보다 **먼저**여야 한다(트랙 L5). 대기 중이던
                // 태스크가 사용자의 OnEndSimulation 코드보다 먼저 취소돼야 "구독만 있고
                // 해지가 없는" 상태가 한 프레임도 남지 않는다. TearDown이 지키던 순서를
                // 네이티브 구동에서도 그대로 지킨다.
                b.Scope.Cancel();

                // 짝이 열리지 않았으면 닫지 않는다(LC1). Scope.Cancel은 그 위에서
                // 무조건 돈다 — 다른 훅이 건 구독·정리가 있을 수 있고, 그것은
                // OnBeginSimulation의 성공과 무관하다.
                if (b.BeginSucceeded)
                {
                    Invoke(b, static x => x.OnEndSimulation(), nameof(Component.OnEndSimulation));
                }
                return true;

            case LifecyclePhase.OnUninitializing:
                Invoke(b, static x => x.OnUninitializing(), nameof(Component.OnUninitializing));
                return true;
            case LifecyclePhase.OnBeginSimulation:
                // 네이티브 드레인이 "OnInitialized 다음 정거장"으로 부른다. 꺼져 있으면
                // 네이티브 쪽 게이트에서 이미 걸러지지만, 관리 측 Enabled는 따로
                // 꺼질 수 있으므로 여기서도 본다(옛 _pendingStart 드레인과 같은 조건).
                if (!b.Enabled) return true;

                // 실패하면 루틴을 시작하지 않는다(LC1). 예전에는 Invoke 다음 줄이
                // 바로 StartSimulation이라, 훅이 던져 **Invoke가 방금 이 인스턴스를
                // 껐는데도** 루틴이 시작됐다.
                if (!Invoke(b, static x => x.OnBeginSimulation(), nameof(Component.OnBeginSimulation)))
                {
                    return true;
                }

                // 훅이 끝까지 돌았으므로 이 단계는 **열렸다.** 짝인 OnEndSimulation은
                // 아래 자기 비활성화와 무관하게 와야 한다 — 훅 안에서 잡은 것을
                // 그 짝에서 놓는 형태가 흔하다.
                b.MarkBeginSucceeded();

                // 훅 안에서 스스로 껐거나 자기를 제거했으면 그 뜻을 존중한다(LC1).
                // "단계가 열렸는가"와 "루틴을 시작하는가"는 다른 물음이다 — 처음에
                // 이 둘을 한 줄로 뭉갰더니, 정상적으로 끝난 시작 훅이 짝을 못 받았다
                // (실패 픽스처 트레이스가 잡아 줬다).
                //
                // 예외와도 성격이 다르다. 실패는 격리이고 이쪽은 정상적인 의사
                // 표현이라, 뒤에 붙는 정책(재시도·진단)이 갈려야 한다.
                if (!b.Enabled || !b.IsAlive) return true;

                StartSimulation(b);
                return true;
            default:
                Native.Log(2, $"[생명주기] 알 수 없는 단계가 들어왔다: {(LifecyclePhase)phase}");
                return false;
        }
    }

    /// <summary>
    /// 네이티브가 활성 전이 하나를 이 인스턴스에 전달한다(트랙 L · 활성 축).
    ///
    /// 부르는 곳은 <c>ScriptComponent::OnEnable/OnDisable</c> 하나뿐이고, 그것은
    /// <c>Component::SetEnabled</c>가 **전이일 때만** 부른다 — 즉 여기까지 온 것은
    /// 이미 "값이 실제로 바뀌었다"는 뜻이다. 그래도 <see cref="ApplyEnabled"/>가
    /// 한 번 더 비교하는 이유는, 관리 측이 먼저 바꾼 뒤 네이티브를 부른 경우
    /// (Component.Enabled의 폴백 경로)와 창구를 공유하기 때문이다.
    ///
    /// ── IsAlive를 보지 않는다 ──
    ///
    /// 6단계 전송로와 다른 점이다. 활성 전이는 파괴 표시된 오브젝트에도 온다
    /// (<c>Entity::SetEnabled</c>는 생존을 묻지 않는다). 여기서 IsAlive로 걸러내면
    /// 축소가 경계에서 버려지던 것과 같은 종류의 조용한 소실이 하나 더 생긴다.
    /// </summary>
    public static bool DispatchEnabled(int instanceId, bool enabled)
    {
        var b = ScriptFactory.Find(instanceId);
        if (b is null) return false;

        ApplyEnabled(b, enabled);
        return true;
    }

    /// <summary>
    /// 활성 상태를 세우고 짝이 되는 훅을 부른다. 네이티브 전달과 관리 측 폴백이
    /// 공유하는 <b>유일한</b> 자리다 — 훅을 부르는 곳이 둘이면 그 둘이 어긋난다.
    ///
    /// 초기화 전 인스턴스에는 훅을 흘리지 않는다("Initialized 없이 그 다음 없음",
    /// <see cref="Component.IsInitialized"/>). 값은 그래도 세운다 — 나중에
    /// 초기화될 때 틱 게이트가 옳은 값을 봐야 한다.
    /// </summary>
    internal static void ApplyEnabled(Component b, bool enabled)
    {
        if (!b.SetEnabledState(enabled)) return;
        if (!b.InitializeSucceeded) return;

        if (enabled) Invoke(b, static x => x.OnEnable(), nameof(Component.OnEnable));
        else Invoke(b, static x => x.OnDisable(), nameof(Component.OnDisable));
    }

    /// <summary>
    /// 시뮬레이션 본문을 띄운다 (설계 문서 §4 트랙 L5). OnBeginSimulation 직후다.
    ///
    /// 태스크를 어디에도 보관하지 않는 이유: 수명은 <see cref="SimulationScope"/>가
    /// 쥔다. 제거 시 TearDown이 Scope.Cancel()을 먼저 부르고, 대기 중이던
    /// Scope.Delay가 그 자리에서 취소되며 본문이 풀린다 — 목록을 따로 들면 그
    /// 단일 소유가 둘로 갈린다.
    /// </summary>
    private static void StartSimulation(Component b)
    {
        Task task;
        try
        {
            // 첫 await 전까지는 동기 구간이다 — 여기서 터지면 예외가 그대로 올라온다.
            task = b.OnSimulate() ?? Task.CompletedTask;
        }
        catch (Exception ex)
        {
            // ① 동기 throw — async를 붙이지 않은 본문이 첫 await 전에 던졌다.
            FaultSimulation(b, ex);
            return;
        }

        if (task.IsCompleted)
        {
            // ② 즉시 faulted — async 본문이 첫 await 전에 던지면 컴파일러가 예외를
            //    이미 완료된 faulted Task로 감싸 반환한다. 본문이 없거나(기본 구현)
            //    await 없이 정상 종료한 경우도 여기로 온다.
            ReportSimulationFault(b, task);
            return;
        }

        // 관측되지 않은 태스크 예외는 GC 시점에 터져 원인 지점과 멀어진다 —
        // 여기서 거둬 스크립트 이름과 함께 남긴다. ExecuteSynchronously를 주는 이유는
        // 완료가 게임 스레드(Scope.Tick)에서 일어나므로 그 자리에서 처리하기 위해서다.
        task.ContinueWith(
            t => ReportSimulationFault(b, t),
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private static void ReportSimulationFault(Component b, Task task)
    {
        // 취소는 정상 종료다 — 엔티티 제거가 그 경로다(설계 문서 §4 트랙 L5).
        // 이 조기 반환이 "취소와 fault를 혼동하지 않는다"(LC5)를 지킨다.
        if (!task.IsFaulted) return;

        // ②③ 두 갈래가 여기로 모인다. 정책은 ①과 같아야 한다.
        FaultSimulation(b, task.Exception!);
    }

    /// <summary>
    /// 시뮬레이션 본문 실패의 <b>유일한</b> 정책 지점 (LC5 · 2026-09-05).
    ///
    /// ── 왜 하나여야 하는가 ──
    ///
    /// 같은 논리적 실패가 세 갈래로 도착한다:
    ///
    ///   ① 동기 throw        — async 없는 본문의 첫 await 전 예외
    ///   ② 즉시 faulted Task — async 본문의 첫 await 전 예외(컴파일러가 감싼다)
    ///   ③ 나중 faulted Task — await를 지난 뒤의 예외
    ///
    /// 저작자가 <c>async</c>를 붙이느냐 마느냐는 취향에 가까운데, 예전에는 그것으로
    /// 엔진의 실패 정책이 갈렸다 — ①만 인스턴스를 끄고 ②③은 로그만 남겼다.
    /// 실측(verify-lifecycle-failure 판정 F): ① 실패 후 틱 0회, ③ 13회.
    /// 죽은 루틴을 가진 인스턴스가 아무 일 없다는 듯 계속 돌았다.
    ///
    /// ── 왜 '끈다'로 통일하는가 ──
    ///
    /// LC1이 정한 방향과 같다 — 실패한 것은 격리하고, 되살리는 것은 명시적
    /// 재생성 경계(RetryInstance·script.reload)로만 한다. 반대 방향(둘 다 로그만)은
    /// 본문이 죽은 것을 행동으로는 알 수 없게 만들어 더 나쁘다.
    ///
    /// ── 스레드 ──
    ///
    /// ③은 continuation에서 온다. 지원 경로(<c>Scope.Delay</c>)의 완료는
    /// <c>SimulationScope.Tick</c>이 게임 스레드에서 일으키므로 여기도 게임
    /// 스레드다. 외부 Task를 await하면 워커에서 올 수 있는데, 그때
    /// <c>Enabled</c> 대입은 네이티브를 건드린다. 그래서 스레드를 확인하고,
    /// 아니면 상태를 바꾸지 않고 진단만 남긴다.
    ///
    /// <b>이것은 진단이지 강제가 아니다.</b> 외부 await 자체를 막지 않으며,
    /// 그 경계를 실제로 닫는 것은 LC5의 남은 갈래다.
    /// </summary>
    private static void FaultSimulation(Component b, Exception ex)
    {
        if (!Native.IsGameThread)
        {
            Native.Log(3,
                $"[{b.GetType().Name}] OnSimulate 예외가 게임 스레드 밖에서 도착했다 — " +
                $"외부 Task를 await한 경로다. 상태를 바꾸지 않고 남긴다(지원 범위 밖).\n{ex}");
            return;
        }

        Native.Log(3, $"[{b.GetType().Name}] OnSimulate 예외 — 이 스크립트를 비활성화합니다.\n{ex}");
        b.Enabled = false;
    }

    /// <summary>
    /// 스크립트 하나의 예외가 에디터 전체를 죽이지 않게 한다.
    /// 같은 예외가 매 프레임 반복되어 로그가 폭주하지 않도록 해당 스크립트만 끈다(설계 문서 10.2).
    /// </summary>
    /// <returns>
    /// 예외 없이 끝났으면 true (LC1 · 2026-09-05).
    ///
    /// 예전에는 void였다. 그래서 호출자가 "전달했다"와 "성공했다"를 구분할 수 없었고,
    /// 실패한 단계 뒤에 다음 단계가 그대로 이어졌다. 반환값을 무시해도 되는 자리가
    /// 있지만(축소 훅처럼 이미 짝 검사를 지난 것들), 여는 훅에서는 반드시 봐야 한다.
    /// </returns>
    private static bool Invoke(Component b, Action<Component> call, string phase)
    {
        try
        {
            call(b);
            return true;
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[{b.GetType().Name}] {phase} 예외 — 이 스크립트를 비활성화합니다.\n{ex}");
            b.Enabled = false;
            return false;
        }
    }

    /// <summary>
    /// 프레임 중반의 등록 반영. 이름이 Awake였던 시절의 두 번째 일(새 인스턴스 깨우기)은
    /// 네이티브 Scene::DrainPendingLifecycle이 가져갔고, 남은 것은 Flush 하나다.
    /// </summary>
    public static void FlushRegistrations()
    {
        Flush();
    }

    /// <summary>
    /// 물리 스텝 **앞**의 틱 (설계 문서 §4 트랙 L5). 프레임에서 관리 측이 처음
    /// 닿는 자리이므로 멤버십 반영(Flush)과 스코프 시간 진행도 여기서 한다.
    ///
    /// 스코프를 여기서 흘리는 이유: <c>await Scope.Delay</c>가 프레임의 일이
    /// 시작되기 전에 재개돼야 그 프레임 안에서 판단할 수 있다. 옛 구조에서는
    /// Update(물리 뒤) 자리라 한 프레임 늦게 깨어났다.
    /// </summary>
    public static void PrePhysicsTick(float dt)
    {
        Flush();

        for (int i = 0; i < _active.Count; ++i)
        {
            var b = _active[i];
            if (!b.IsAlive) continue;

            // 시뮬레이션 스코프는 Enabled와 무관하게 흐른다 — 꺼진 스크립트도 대기 중인
            // Scope.Delay는 계속 흘러야 한다(설계 문서 §4 트랙 L2).
            b.Scope.Tick(dt);

            if (!b.Enabled) continue;
            Invoke(b, x => x.PrePhysics(dt), nameof(Component.PrePhysics));
        }
    }

    /// <summary>물리 스텝 **뒤**의 틱. 옛 Update·LateUpdate가 함께 여기로 왔다.</summary>
    public static void PostPhysicsTick(float dt)
    {
        for (int i = 0; i < _active.Count; ++i)
        {
            var b = _active[i];
            if (!b.IsAlive || !b.Enabled) continue;
            Invoke(b, x => x.PostPhysics(dt), nameof(Component.PostPhysics));
        }

        Flush();
    }

    /// <summary>
    /// 물리 이벤트 하나를 해당 스크립트에 전달한다.
    /// 예외 격리는 다른 콜백과 같게 처리한다 — 충돌 콜백 하나가 프레임 전체를 죽이지 않는다.
    /// </summary>
    public static void DispatchPhysics(Component target, PhysicsEventKind kind, in Collision collision)
    {
        try
        {
            switch (kind)
            {
                case PhysicsEventKind.TriggerEnter:   target.OnTriggerEnter(in collision);   break;
                case PhysicsEventKind.TriggerStay:    target.OnTriggerStay(in collision);    break;
                case PhysicsEventKind.TriggerExit:    target.OnTriggerExit(in collision);    break;
                case PhysicsEventKind.CollisionEnter: target.OnCollisionEnter(in collision); break;
                case PhysicsEventKind.CollisionStay:  target.OnCollisionStay(in collision);  break;
                case PhysicsEventKind.CollisionExit:  target.OnCollisionExit(in collision);  break;
            }
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[{target.GetType().Name}] {kind} 예외 — 이 스크립트를 비활성화합니다.\n{ex}");
            target.Enabled = false;
        }
    }

    /// <summary>
    /// 이름으로 부르는 콜백을 전달한다(애니메이션 키프레임 이벤트·입력 액션).
    ///
    /// 물리 콜백과 같은 격리 규약을 쓴다 — 하나가 던진 예외로 프레임이 죽지 않도록
    /// 해당 스크립트만 끈다. 다만 이름이 안 맞아 못 찾은 것은 예외가 아니라
    /// 데이터와 코드가 어긋난 것이므로 경고만 남기고 넘어간다.
    /// </summary>
    public static void DispatchMessage(Component target, string message)
    {
        try
        {
            if (!target.InvokeMessage(message))
            {
                Native.Log(2, $"[{target.GetType().Name}] '{message}' 메서드를 찾지 못했습니다 " +
                              "(public 무인자 void 메서드여야 합니다).");
            }
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[{target.GetType().Name}] '{message}' 예외 — 이 스크립트를 비활성화합니다.\n{ex}");
            target.Enabled = false;
        }
    }

    /// <summary>
    /// 소유자가 사라졌는데 제거 경로를 타지 않은 인스턴스를 거둔다. 거둔 수를 돌려준다.
    ///
    /// ── 왜 Clear가 아니라 이것인가 ──
    ///
    /// 씬 언로드에서 <see cref="Clear"/>를 부르면 <b>DontDestroyOnLoad 오브젝트의
    /// 스크립트까지 죽는다</b>. Scene::AllDestroyMark가 DDOL을 건너뛰므로 그 컴포넌트는
    /// 파괴 표시조차 되지 않고, 따라서 여기 _active에 그대로 남아 있다. Clear는 목록을
    /// 통째로 도니 그것들에도 OnDisable·OnDestroy를 부르고 목록에서 지운다 —
    /// 오브젝트는 살아서 다음 씬으로 넘어가는데 스크립트만 죽는 셈이다.
    /// 크래시가 아니라 '저 오브젝트만 스크립트가 안 돈다'로 나타나 원인을 짚기 어렵다.
    ///
    /// 그래서 소유자 생존으로 가른다. 세대 핸들 비교라 슬롯 재사용에도 속지 않는다.
    ///
    /// 이미 파괴 표시된 것은 건드리지 않는다 — 정상 경로(ScriptComponent::OnDestroy →
    /// Remove → _pendingRemove)를 탄 것이고, Flush가 TearDown(OnEndSimulation→
    /// OnRemovingFromScene→OnUninitializing)을 부를 예정이라 여기서 또 부르면 두 번 불린다.
    /// </summary>
    public static int SweepOrphans()
    {
        int swept = 0;

        // _active를 뒤에서부터 훑는다 — Remove가 _byObject를 건드리므로 순회 중
        // 앞에서부터 지우면 자리가 밀린다. Remove 자체는 _active를 손대지 않지만
        // (지연 제거) 규약을 지켜 두는 편이 나중에 안전하다.
        for (int i = _active.Count - 1; i >= 0; --i)
        {
            Component b = _active[i];
            if (b.IsMarkedDestroyed) continue;   // 정상 경로가 이미 잡았다
            if (b.Entity.IsAlive) continue;  // 살아 있다 — DDOL 포함

            // 정상 경로와 같은 통로로 보낸다. 여기서 직접 OnDestroy를 부르지 않는 이유는
            // 그러면 _active·_byObject 정리가 빠져 목록에만 시체가 남기 때문이다.
            Remove(b);
            ++swept;
        }

        return swept;
    }

    /// <summary>
    /// 전부 정리한다. <b>어셈블리 리로드 전용이다</b> — 그때는 스크립트 타입을 가리키는
    /// 참조를 하나도 남기면 안 되므로 살아 있는 것까지 포함해 끊어야 한다.
    ///
    /// 씬 언로드에는 쓰지 않는다(위 <see cref="SweepOrphans"/>의 DDOL 설명 참고).
    /// </summary>
    public static void Clear()
    {
        // ★ 보류 큐를 먼저 반영한다(2026-09-05).
        //
        // 관리 틱이 한 번도 돌지 않은 인스턴스는 _pendingAdd에 머문다 — 편집
        // 모드에는 관리 틱이 없으므로 거기서 만들어진 스크립트가 전부 그렇다.
        // 아래 루프는 _active만 훑으므로, 이 Flush가 없으면 그것들은 축소 훅을
        // **한 번도 받지 못한 채** 사라진다(실측: 정지 후 복원된 인스턴스가
        // OnUninitializing을 영영 못 받았다). 잡은 것이 전부 그대로 샌다.
        Flush();

        foreach (var b in _active)
        {
            if (!b.InitializeSucceeded) continue;   // Flush로 목록에만 들어오고 아직 초기화되지 않은 것

            // ApplyEnabled를 거친다 — 훅을 부르는 자리가 둘이면 그 둘이 어긋난다
            // (여기서 직접 부르면 _enabled가 true로 남아 상태와 훅이 갈라졌다).
            // 네이티브를 부르지 않으므로 어셈블리 리로드·종료 경로에서도 안전하다.
            ApplyEnabled(b, false);
            TearDown(b);
        }

        _active.Clear();
        _pendingAdd.Clear();
        _pendingRemove.Clear();
        _byObject.Clear();
    }
}

