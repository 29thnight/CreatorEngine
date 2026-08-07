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

    // Awake/Start는 각각 한 번만 불러야 한다.
    private static readonly List<Behaviour> _pendingAwake = new();
    private static readonly List<Behaviour> _pendingStart = new();

    public static int ActiveCount => _active.Count;

    public static void Add(Behaviour behaviour)
    {
        _pendingAdd.Add(behaviour);
        _pendingAwake.Add(behaviour);

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

                // 깨어난 적 없는 인스턴스에는 OnDestroy를 부르지 않는다(Unity와 같은 규약).
                // Awake에서 잡은 것을 OnDestroy에서 놓는 것이 흔한 형태라,
                // 짝이 맞지 않으면 초기화하지 않은 상태를 정리하려 든다.
                if (b.IsAwakened) Invoke(b, static x => x.OnDestroy(), nameof(Behaviour.OnDestroy));
            }
            _pendingRemove.Clear();
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
    /// 아직 깨우지 않은 스크립트만 Awake/OnEnable 시킨다.
    ///
    /// <see cref="Flush"/>를 부르지 않는 것이 핵심이다 — 활성 목록을 건드리지 않으므로
    /// Update 순회 도중에 불러도 안전하다. 프리팹을 스폰한 직후 이것을 호출하면
    /// Instantiate가 반환되기 전에 Awake가 끝나 Unity와 같은 순서가 된다.
    /// 목록 편입(따라서 첫 Update)은 다음 틱 경계로 미뤄진다.
    /// </summary>
    public static void AwakeNewlyCreated()
    {
        if (_pendingAwake.Count == 0) return;

        // Awake 안에서 또 스폰할 수 있으므로 복사본을 돈다.
        var batch = _pendingAwake.ToArray();
        _pendingAwake.Clear();

        foreach (var b in batch)
        {
            // 대기하는 동안 파괴되었을 수 있다 — 재생 전환 때 원본 씬 인스턴스가 접히는 경우다.
            if (!b.IsAlive) continue;

            b.MarkAwakened();
            Invoke(b, static x => x.Awake(), nameof(Behaviour.Awake));
            if (b.Enabled) Invoke(b, static x => x.OnEnable(), nameof(Behaviour.OnEnable));
            _pendingStart.Add(b);
        }
    }

    public static void Update(float dt)
    {
        Flush();

        // Start는 첫 Update 직전에 한 번(Unity와 같은 순서).
        if (_pendingStart.Count > 0)
        {
            var batch = _pendingStart.ToArray();
            _pendingStart.Clear();
            foreach (var b in batch)
            {
                if (b.IsAlive && b.Enabled) Invoke(b, static x => x.Start(), nameof(Behaviour.Start));
            }
        }

        for (int i = 0; i < _active.Count; ++i)
        {
            var b = _active[i];
            if (!b.IsAlive || !b.Enabled) continue;
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

    /// <summary>씬 언로드 — 전부 정리한다.</summary>
    public static void Clear()
    {
        foreach (var b in _active)
        {
            if (!b.IsAwakened) continue;   // Flush로 목록에만 들어오고 아직 깨어나지 않은 것

            if (b.Enabled) Invoke(b, static x => x.OnDisable(), nameof(Behaviour.OnDisable));
            Invoke(b, static x => x.OnDestroy(), nameof(Behaviour.OnDestroy));
        }

        _active.Clear();
        _pendingAdd.Clear();
        _pendingRemove.Clear();
        _pendingAwake.Clear();
        _pendingStart.Clear();
        _byObject.Clear();
    }
}

