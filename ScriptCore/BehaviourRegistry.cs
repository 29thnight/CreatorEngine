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

        ObjectHandle owner = behaviour.GameObject.Handle;
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

        ObjectHandle owner = behaviour.GameObject.Handle;
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
                Invoke(b, static x => x.OnRemovingFromScene(), nameof(Behaviour.OnRemovingFromScene));
                return true;
            case LifecyclePhase.OnBeginSimulation:
                // 네이티브 드레인이 "OnInitialized 다음 정거장"으로 부른다. 꺼져 있으면
                // 네이티브 쪽 게이트에서 이미 걸러지지만, 관리 측 Enabled는 따로
                // 꺼질 수 있으므로 여기서도 본다(옛 _pendingStart 드레인과 같은 조건).
                if (b.Enabled) Invoke(b, static x => x.OnBeginSimulation(), nameof(Behaviour.OnBeginSimulation));
                return true;
            default:
                // 뒤쪽 두 단계(OnEndSimulation·OnUninitializing)는 아직 TearDown이 부른다.
                Native.Log(2, $"[생명주기] 아직 전달하지 않는 단계가 들어왔다: {(LifecyclePhase)phase}");
                return false;
        }
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

    public static void Awake()
    {
        Flush();
        AwakeNewlyCreated();
    }

    /// <summary>
    /// 남겨 둔 빈 진입점. 앞쪽 세 단계가 네이티브 구동으로 옮겨가며(트랙 L · L3 잔여
    /// 2단계) 할 일이 없어졌다 — 네이티브 Scene::DrainPendingLifecycle이 그 자리를
    /// 대신한다.
    ///
    /// 진입점을 남기는 이유: 네이티브 ClrHost::FlushPendingAwake가 이것을 부르고,
    /// 그 호출부가 Api_Prefab_Instantiate의 "Instantiate가 반환되기 전에 Awake가
    /// 끝난다"(Unity 순서) 보장이다. 그 보장은 이제 바로 앞줄의
    /// scene->DrainPendingLifecycle()이 동기로 세운다 — 네이티브 드레인이
    /// ScriptComponent::OnInitialized를 부르고 그것이 곧 관리 측 OnInitialized다.
    /// </summary>
    public static void AwakeNewlyCreated()
    {
    }

    public static void Update(float dt)
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
            Invoke(b, x => x.Update(dt), nameof(Behaviour.Update));
        }
    }

    public static void FixedUpdate(float dt)
    {
        for (int i = 0; i < _active.Count; ++i)
        {
            var b = _active[i];
            if (!b.IsAlive || !b.Enabled) continue;
            Invoke(b, x => x.FixedUpdate(dt), nameof(Behaviour.FixedUpdate));
        }
    }

    public static void LateUpdate(float dt)
    {
        for (int i = 0; i < _active.Count; ++i)
        {
            var b = _active[i];
            if (!b.IsAlive || !b.Enabled) continue;
            Invoke(b, x => x.LateUpdate(dt), nameof(Behaviour.LateUpdate));
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
            if (b.GameObject.IsAlive) continue;  // 살아 있다 — DDOL 포함

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

