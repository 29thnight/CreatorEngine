namespace CreatorEngine;

/// <summary>
/// 살아 있는 스크립트를 모아 두고 틱마다 순회한다.
///
/// 경계는 틱당 한 번만 넘고 여기서부터는 전부 관리 영역이다(설계 문서 02절).
/// 순회 도중 스크립트가 추가·삭제될 수 있으므로 보류 큐를 두고 틱 경계에서 반영한다 —
/// 네이티브의 <c>AllDestroyMark</c> 지연 파괴와 같은 철학이다.
/// </summary>
internal static class BehaviourRegistry
{
    private static readonly List<Behaviour> _active = new();
    private static readonly List<Behaviour> _pendingAdd = new();
    private static readonly List<Behaviour> _pendingRemove = new();

    /// <summary>
    /// 오브젝트별 스크립트 목록. GetComponent가 경계를 넘지 않고 답하기 위한 색인이다.
    ///
    /// 실측에서 GetComponent 계열이 734회로 1위인데, 그 상당수가 스크립트끼리의 참조다.
    /// 이 색인이 있으면 그 호출들이 전부 관리 영역에서 끝난다(설계 문서 03.1 · 11.2).
    /// 보류 큐를 거치지 않고 즉시 갱신하는 이유는, 스폰 직후 Awake 안에서 바로
    /// GetComponent를 부르는 패턴이 흔하기 때문이다.
    /// </summary>
    private static readonly Dictionary<ObjectHandle, List<Behaviour>> _byObject = new();

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

    public static void Add(Behaviour behaviour)
    {
        // _pendingAdd는 **틱 멤버십**이다(_active 편입) — 생명주기 단계와 무관하며
        // "경계는 틱당 1회"의 근거라 그대로 둔다.
        _pendingAdd.Add(behaviour);

        ObjectHandle owner = behaviour.Entity.Handle;
        if (!_byObject.TryGetValue(owner, out var list))
        {
            list = new List<Behaviour>(2);
            _byObject[owner] = list;
        }
        list.Add(behaviour);
    }

    public static void Remove(Behaviour behaviour)
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
    /// 관리 스크립트만 이 색인에 들어 있으므로 실제로 걸리는 것은 Behaviour 파생뿐이다.
    /// </summary>
    public static T? FindComponent<T>(ObjectHandle owner) where T : Component
    {
        if (!_byObject.TryGetValue(owner, out var list)) return null;

        for (int i = 0; i < list.Count; ++i)
        {
            // 살아 있는지는 Behaviour 쪽에서 본다 — T는 Component까지만 보장되기 때문이다.
            Behaviour entry = list[i];
            if (entry.IsAlive && entry is T match) return match;
        }
        return null;
    }

    /// <summary>오브젝트에 붙은 T 타입 스크립트 전부.</summary>
    public static List<T> FindAll<T>(ObjectHandle owner) where T : Behaviour
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

                // Initialized 없이 Uninitializing 없음(설계 문서 §4 트랙 L, Behaviour.cs
                // IsInitialized 참고). OnInitialized에서 잡은 것을 OnUninitializing에서
                // 놓는 것이 흔한 형태라, 짝이 맞지 않으면 초기화하지 않은 상태를
                // 정리하려 든다.
                if (b.IsInitialized) TearDown(b);
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
    private static void TearDown(Behaviour b)
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
        Invoke(b, static x => x.OnEndSimulation(), nameof(Behaviour.OnEndSimulation));
        Invoke(b, static x => x.OnRemovingFromScene(), nameof(Behaviour.OnRemovingFromScene));
        Invoke(b, static x => x.OnUninitializing(), nameof(Behaviour.OnUninitializing));
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
        if (b is null || !b.IsAlive) return false;

        // OnInitialized는 그 가드 **앞**이다 — 이 단계가 곧 IsInitialized를 세운다.
        if ((LifecyclePhase)phase == LifecyclePhase.OnInitialized)
        {
            if (b.IsInitialized) return false;   // 두 번 초기화하지 않는다
            b.MarkInitialized();
            Invoke(b, static x => x.OnInitialized(), nameof(Behaviour.OnInitialized));
            return true;
        }

        // 초기화 전 인스턴스에 중간 단계를 흘리면 "Initialized 없이 그 다음"이 되어
        // 설계 문서 §4 트랙 L의 순서 규약이 깨진다.
        if (!b.IsInitialized) return false;

        switch ((LifecyclePhase)phase)
        {
            case LifecyclePhase.OnAddedToScene:
                Invoke(b, static x => x.OnAddedToScene(), nameof(Behaviour.OnAddedToScene));

                // 최초 진입에서만 OnEnable을 이어 붙인다. 활성 축은 6단계와 직교라
                // 네이티브 단계로 오지 않는데, 예전 드레인이 이 자리에서
                // (OnAddedToScene 직후) 불러 왔으므로 그 순서를 보존한다. 이송
                // 재부착은 '최초'가 아니므로 다시 불리지 않는다.
                if (!b.EnterDelivered)
                {
                    b.MarkEnterDelivered();
                    if (b.Enabled) Invoke(b, static x => x.OnEnable(), nameof(Behaviour.OnEnable));
                }
                return true;
            case LifecyclePhase.OnRemovingFromScene:
                // 이송에서도 파괴에서도 온다. 이송은 여러 번 정상 발화하므로 여기서
                // 막지 않는다 — 파괴 경로의 중복은 TeardownDelivered가 TearDown 쪽에서 가른다.
                Invoke(b, static x => x.OnRemovingFromScene(), nameof(Behaviour.OnRemovingFromScene));
                return true;

            case LifecyclePhase.OnEndSimulation:
                // 축소의 시작이다 — 여기서 묶음 표시를 세운다.
                b.MarkTeardownDelivered();

                // ★ 취소가 OnEndSimulation보다 **먼저**여야 한다(트랙 L5). 대기 중이던
                // 태스크가 사용자의 OnEndSimulation 코드보다 먼저 취소돼야 "구독만 있고
                // 해지가 없는" 상태가 한 프레임도 남지 않는다. TearDown이 지키던 순서를
                // 네이티브 구동에서도 그대로 지킨다.
                b.Scope.Cancel();
                Invoke(b, static x => x.OnEndSimulation(), nameof(Behaviour.OnEndSimulation));
                return true;

            case LifecyclePhase.OnUninitializing:
                Invoke(b, static x => x.OnUninitializing(), nameof(Behaviour.OnUninitializing));
                return true;
            case LifecyclePhase.OnBeginSimulation:
                // 네이티브 드레인이 "OnInitialized 다음 정거장"으로 부른다. 꺼져 있으면
                // 네이티브 쪽 게이트에서 이미 걸러지지만, 관리 측 Enabled는 따로
                // 꺼질 수 있으므로 여기서도 본다(옛 _pendingStart 드레인과 같은 조건).
                if (b.Enabled)
                {
                    Invoke(b, static x => x.OnBeginSimulation(), nameof(Behaviour.OnBeginSimulation));
                    StartSimulation(b);
                }
                return true;
            default:
                Native.Log(2, $"[생명주기] 알 수 없는 단계가 들어왔다: {(LifecyclePhase)phase}");
                return false;
        }
    }

    /// <summary>
    /// 시뮬레이션 본문을 띄운다 (설계 문서 §4 트랙 L5). OnBeginSimulation 직후다.
    ///
    /// 태스크를 어디에도 보관하지 않는 이유: 수명은 <see cref="SimulationScope"/>가
    /// 쥔다. 제거 시 TearDown이 Scope.Cancel()을 먼저 부르고, 대기 중이던
    /// Scope.Delay가 그 자리에서 취소되며 본문이 풀린다 — 목록을 따로 들면 그
    /// 단일 소유가 둘로 갈린다.
    /// </summary>
    private static void StartSimulation(Behaviour b)
    {
        Task task;
        try
        {
            // 첫 await 전까지는 동기 구간이다 — 여기서 터지면 예외가 그대로 올라온다.
            task = b.OnSimulate() ?? Task.CompletedTask;
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[{b.GetType().Name}] OnSimulate 시작 예외 — 이 스크립트를 비활성화합니다.\n{ex}");
            b.Enabled = false;
            return;
        }

        if (task.IsCompleted)
        {
            // 본문이 없거나(기본 구현) await 없이 끝났다. 예외만 확인하고 끝낸다.
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

    private static void ReportSimulationFault(Behaviour b, Task task)
    {
        // 취소는 정상 종료다 — 엔티티 제거가 그 경로다(설계 문서 §4 트랙 L5).
        if (!task.IsFaulted) return;

        Native.Log(3, $"[{b.GetType().Name}] OnSimulate 예외\n{task.Exception}");
    }

    /// <summary>
    /// 스크립트 하나의 예외가 에디터 전체를 죽이지 않게 한다.
    /// 같은 예외가 매 프레임 반복되어 로그가 폭주하지 않도록 해당 스크립트만 끈다(설계 문서 10.2).
    /// </summary>
    private static void Invoke(Behaviour b, Action<Behaviour> call, string phase)
    {
        try
        {
            call(b);
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[{b.GetType().Name}] {phase} 예외 — 이 스크립트를 비활성화합니다.\n{ex}");
            b.Enabled = false;
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
            Invoke(b, x => x.PrePhysics(dt), nameof(Behaviour.PrePhysics));
        }
    }

    /// <summary>물리 스텝 **뒤**의 틱. 옛 Update·LateUpdate가 함께 여기로 왔다.</summary>
    public static void PostPhysicsTick(float dt)
    {
        for (int i = 0; i < _active.Count; ++i)
        {
            var b = _active[i];
            if (!b.IsAlive || !b.Enabled) continue;
            Invoke(b, x => x.PostPhysics(dt), nameof(Behaviour.PostPhysics));
        }

        Flush();
    }

    /// <summary>
    /// 물리 이벤트 하나를 해당 스크립트에 전달한다.
    /// 예외 격리는 다른 콜백과 같게 처리한다 — 충돌 콜백 하나가 프레임 전체를 죽이지 않는다.
    /// </summary>
    public static void DispatchPhysics(Behaviour target, PhysicsEventKind kind, in Collision collision)
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
    public static void DispatchMessage(Behaviour target, string message)
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
            Behaviour b = _active[i];
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
        foreach (var b in _active)
        {
            if (!b.IsInitialized) continue;   // Flush로 목록에만 들어오고 아직 초기화되지 않은 것

            if (b.Enabled) Invoke(b, static x => x.OnDisable(), nameof(Behaviour.OnDisable));
            TearDown(b);
        }

        _active.Clear();
        _pendingAdd.Clear();
        _pendingRemove.Clear();
        _byObject.Clear();
    }
}

