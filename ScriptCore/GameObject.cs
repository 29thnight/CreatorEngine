namespace CreatorEngine;

/// <summary>
/// 네이티브 GameObject의 얇은 얼굴. 상태는 핸들 하나뿐이고 실제 데이터는 전부 네이티브에 있다.
/// </summary>
public readonly struct GameObject(ObjectHandle handle) : IEquatable<GameObject>
{
    internal readonly ObjectHandle Handle = handle;

    /// <summary>파괴된 객체를 가리키면 false. 세대 비교 한 번으로 끝난다.</summary>
    public bool IsAlive => Handle.IsValid && Native.IsAlive(Handle);

    public string Name => Native.GetName(Handle);

    /// <summary>
    /// 엔진에서 Transform은 컴포넌트가 아니라 GameObject의 값 멤버다(설계 문서 11.1).
    /// 그래서 컴포넌트 조회가 아니라 프로퍼티로 노출한다.
    /// </summary>
    public Transform Transform => new(Handle);

    public void SetEnabled(bool enabled) => Native.SetEnabled(Handle, enabled);

    // ── 컴포넌트 조회 ──
    //
    // 지금은 관리 스크립트(Behaviour 파생)만 찾는다. 이 호출들은 경계를 넘지 않고
    // C# 안에서 끝나며, 실측 GetComponent 734회의 상당수가 여기 해당한다.
    //
    // 네이티브 컴포넌트(Animator·SoundComponent 등)는 아직 래퍼가 없다.
    // 제약을 타입 파라미터로 못 박아 두면 "찾았는데 null"이 아니라 컴파일 단계에서
    // 걸리므로, 나중에 래퍼가 생겼을 때 오버로드를 더하기도 쉽다.
    // Transform은 컴포넌트가 아니라 값 멤버라 위의 프로퍼티로 접근한다(11.1절).

    /// <summary>
    /// 붙어 있는 T 컴포넌트 하나. 없으면 null.
    ///
    /// 스크립트(Behaviour 파생)면 관리 영역에서 바로 찾고, 네이티브 래퍼면 존재 확인만
    /// 경계를 넘는다. 분기는 <see cref="ComponentKind{T}"/>가 정적으로 판정해 둔다.
    /// </summary>
    public T? GetComponent<T>() where T : Component
    {
        if (ComponentKind<T>.IsManaged)
        {
            return BehaviourRegistry.FindComponent<T>(Handle);
        }

        if (ComponentKind<T>.Exists is { } exists && ComponentKind<T>.Wrap is { } wrap)
        {
            return exists(Handle) ? wrap(Handle) : null;
        }

        return null;   // 아직 래퍼가 없는 네이티브 컴포넌트
    }

    /// <summary>붙어 있는 T 스크립트 전부. 네이티브 컴포넌트는 오브젝트당 하나뿐이다.</summary>
    public List<T> GetComponents<T>() where T : Behaviour
        => BehaviourRegistry.FindAll<T>(Handle);

    /// <summary>Unity 관례의 Try 형태. 찾으면 true.</summary>
    public bool TryGetComponent<T>(out T component) where T : Component
    {
        T? found = GetComponent<T>();
        component = found!;
        return found is not null;
    }

    /// <summary>붙어 있는지만 확인한다.</summary>
    public bool HasComponent<T>() where T : Component
        => GetComponent<T>() is not null;

    // ── 계층 ──
    //
    // 엔진은 오브젝트를 인덱스로 잇지만, 여기서는 세대 핸들로 받는다.
    // 인덱스는 슬롯이 재사용되면 다른 오브젝트를 가리키게 되는데 핸들은 그것을 걸러 준다.

    public int ChildCount => Native.GetChildCount(Handle);

    /// <summary>범위를 벗어나면 무효 핸들을 담은 GameObject가 나온다(IsAlive로 걸러진다).</summary>
    public GameObject GetChild(int index) => new(Native.GetChild(Handle, index));

    /// <summary>부모. 최상위 오브젝트면 무효 핸들이다.</summary>
    public GameObject Parent => new(Native.GetParent(Handle));

    /// <summary>씬 안에서의 인덱스. 엔진 자료구조와 대조할 때만 쓴다.</summary>
    public int Index => Native.GetIndex(Handle);

    /// <summary>인덱스로 찾는다. 엔진의 <c>GameObject::FindIndex</c>에 해당한다.</summary>
    public static GameObject FindByIndex(int index) => new(Native.FindByIndex(index));

    /// <summary>직계 자식을 순회한다. 순회 도중 자식이 바뀌면 결과가 어긋날 수 있다.</summary>
    public IEnumerable<GameObject> Children
    {
        get
        {
            int count = ChildCount;
            for (int i = 0; i < count; ++i) yield return GetChild(i);
        }
    }

    /// <summary>
    /// 자손에서 T 컴포넌트를 모은다. Unity와 같이 자기 자신도 포함한다.
    /// </summary>
    public List<T> GetComponentsInChildren<T>(bool includeSelf = true) where T : Component
    {
        var result = new List<T>();
        Collect(this, includeSelf, result);
        return result;

        static void Collect(GameObject node, bool includeNode, List<T> into)
        {
            if (includeNode && node.GetComponent<T>() is { } found) into.Add(found);

            int count = node.ChildCount;
            for (int i = 0; i < count; ++i) Collect(node.GetChild(i), true, into);
        }
    }

    /// <summary>자손에서 T를 하나만 찾는다. 없으면 null.</summary>
    public T? GetComponentInChildren<T>(bool includeSelf = true) where T : Component
    {
        if (includeSelf && GetComponent<T>() is { } self) return self;

        int count = ChildCount;
        for (int i = 0; i < count; ++i)
        {
            if (GetChild(i).GetComponentInChildren<T>() is { } found) return found;
        }
        return null;
    }

    /// <summary>조상에서 T를 찾는다. Unity의 GetComponentInParent에 해당한다.</summary>
    public T? GetComponentInParent<T>(bool includeSelf = true) where T : Component
    {
        // 계층이 꼬여 순환이 생겨도 멈추도록 깊이를 제한한다.
        const int maxDepth = 64;

        GameObject node = this;
        for (int depth = 0; depth < maxDepth && node.Handle.IsValid; ++depth)
        {
            if ((includeSelf || depth > 0) && node.GetComponent<T>() is { } found) return found;
            node = node.Parent;
        }
        return null;
    }

    /// <summary>
    /// 파괴를 요청한다. 즉시 사라지지 않고 프레임 경계에서 정리된다 —
    /// 엔진의 지연 파괴(AllDestroyMark)와 같은 규약이라, 이 프레임 안에서는
    /// 아직 IsAlive가 참일 수 있다.
    /// </summary>
    public void Destroy() => Native.DestroyObject(Handle);

    /// <summary>Unity와 같은 감각으로 쓰라고 둔 정적 형태.</summary>
    public static void Destroy(GameObject target) => target.Destroy();

    /// <summary>이름으로 찾는다.</summary>
    public static GameObject Find(string name) => new(Native.FindByName(name));

    public bool Equals(GameObject other) => Handle.Equals(other.Handle);
    public override bool Equals(object? obj) => obj is GameObject other && Equals(other);
    public override int GetHashCode() => Handle.GetHashCode();

    // 같은 오브젝트를 가리키는지 비교한다. 세대까지 보므로, 슬롯이 재사용된
    // 다른 오브젝트를 같다고 판정하지 않는다.
    public static bool operator ==(GameObject a, GameObject b) => a.Equals(b);
    public static bool operator !=(GameObject a, GameObject b) => !a.Equals(b);
    public override string ToString() => IsAlive ? $"GameObject({Name})" : "GameObject(<destroyed>)";
}

/// <summary>
/// GameObject의 위치·회전을 다루는 얼굴. 역시 핸들만 들고 있다.
/// </summary>
public readonly struct Transform(ObjectHandle handle)
{
    internal readonly ObjectHandle Handle = handle;

    // ── 로컬 ──

    public Float3 LocalPosition
    {
        get => Native.GetLocalPosition(Handle);
        set => Native.SetLocalPosition(Handle, value);
    }

    public Quaternion LocalRotation
    {
        get => Native.GetLocalRotation(Handle);
        set => Native.SetLocalRotation(Handle, value);
    }

    public Float3 LocalScale
    {
        get => Native.GetLocalScale(Handle);
        set => Native.SetLocalScale(Handle, value);
    }

    // ── 월드 ──
    //
    // 월드 위치·회전·스케일은 읽기는 캐시된 값이고 쓰기는 부모 역행렬을 거쳐
    // 로컬로 환산된다(엔진 SetWorldPosition/SetWorldRotation과 같은 경로).

    public Float3 WorldPosition
    {
        get => Native.GetWorldPosition(Handle);
        set => Native.SetWorldPosition(Handle, value);
    }

    public Quaternion WorldRotation
    {
        get => Native.GetWorldRotation(Handle);
        set => Native.SetWorldRotation(Handle, value);
    }

    public Float3 WorldScale
    {
        get => Native.GetWorldScale(Handle);
        set => Native.SetWorldScale(Handle, value);
    }

    // ── 방향축 ──
    // 월드 기준이고 정규화되어 있다. 조준·이동에서 가장 많이 쓰인다(GetForward 실측 41회).

    public Float3 Forward => Native.GetForward(Handle);
    public Float3 Right   => Native.GetRight(Handle);
    public Float3 Up      => Native.GetUp(Handle);

    // ── 누적 ──
    // 읽고-고쳐-쓰기로 하면 경계를 두 번 넘는다. 네이티브가 한 번에 처리한다.

    public void Translate(Float3 delta) => Native.AddLocalPosition(Handle, delta);

    /// <summary>기존 회전에 이어 붙인다(엔진 AddRotation과 같은 순서).</summary>
    public void Rotate(Quaternion delta) => Native.AddLocalRotation(Handle, delta);

    // ── 남의 오브젝트를 고칠 때 ──
    //
    // Transform은 핸들만 담은 struct라 GameObject.Transform 프로퍼티가 임시 복사본을
    // 돌려준다. 그래서 obj.Transform.LocalPosition = ... 은 컴파일이 막힌다.
    // 자기 것을 고칠 때는 Behaviour.Transform이 필드라 위의 프로퍼티를 그냥 쓰면 된다.

    public void SetLocalPosition(Float3 p) => Native.SetLocalPosition(Handle, p);
    public void SetLocalRotation(Quaternion q) => Native.SetLocalRotation(Handle, q);
    public void SetLocalScale(Float3 s) => Native.SetLocalScale(Handle, s);

    public void SetWorldPosition(Float3 p) => Native.SetWorldPosition(Handle, p);
    public void SetWorldRotation(Quaternion q) => Native.SetWorldRotation(Handle, q);
    public void SetWorldScale(Float3 s) => Native.SetWorldScale(Handle, s);

    /// <summary>주어진 지점을 바라보게 한다. 위쪽은 +Y로 잡는다.</summary>
    public void LookAt(Float3 target)
    {
        Float3 direction = target - WorldPosition;
        if (direction.LengthSquared < 1e-12f) return;

        Native.SetWorldRotation(Handle, Quaternion.LookRotation(direction));
    }
}

