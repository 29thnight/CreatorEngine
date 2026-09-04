namespace CreatorEngine;

/// <summary>
/// 네이티브 Entity의 얇은 얼굴. 상태는 핸들 하나뿐이고 실제 데이터는 전부 네이티브에 있다.
/// </summary>
public readonly struct Entity(ObjectHandle handle) : IEquatable<Entity>
{
    internal readonly ObjectHandle Handle = handle;

    /// <summary>파괴된 객체를 가리키면 false. 세대 비교 한 번으로 끝난다.</summary>
    public bool IsAlive => Handle.IsValid && Native.IsAlive(Handle);

    public string Name => Native.GetName(Handle);

    /// <summary>
    /// 이 오브젝트의 Transform. UI/Canvas처럼 갖지 않는 오브젝트면 null이다(S3).
    ///
    /// <c>GetComponent&lt;Transform&gt;()</c>과 같은 것을 돌려주는 짧은 이름이다 —
    /// Transform 접근은 실측 1위라 매번 조회를 쓰게 하면 코드가 길어진다.
    /// </summary>
    public Transform? Transform => GetComponent<Transform>();

    public void SetEnabled(bool enabled) => Native.SetEnabled(Handle, enabled);

    // ── 컴포넌트 조회 ──
    //
    // 지금은 관리 스크립트(Component 파생)만 찾는다. 이 호출들은 경계를 넘지 않고
    // C# 안에서 끝나며, 실측 GetComponent 734회의 상당수가 여기 해당한다.
    //
    // 네이티브 컴포넌트는 NativeComponentTable에 래퍼가 등재된 것만 찾을 수 있다.
    // 제약을 타입 파라미터로 못 박아 두면 "찾았는데 null"이 아니라 컴파일 단계에서
    // 걸리므로, 나중에 래퍼가 생겼을 때 오버로드를 더하기도 쉽다.

    /// <summary>
    /// 붙어 있는 T 컴포넌트 하나. 없으면 null.
    ///
    /// 스크립트(Component 파생)면 관리 영역에서 바로 찾고, 네이티브 래퍼면 존재 확인만
    /// 경계를 넘는다. 분기는 <see cref="ComponentKind{T}"/>가 정적으로 판정해 둔다.
    /// </summary>
    public T? GetComponent<T>() where T : Component
    {
        if (ComponentKind<T>.IsManaged)
        {
            return ScriptRegistry.FindComponent<T>(Handle);
        }

        if (ComponentKind<T>.Exists is { } exists && ComponentKind<T>.Wrap is { } wrap)
        {
            return exists(Handle) ? wrap(Handle) : null;
        }

        return null;   // 아직 래퍼가 없는 네이티브 컴포넌트
    }

    /// <summary>붙어 있는 T 스크립트 전부. 네이티브 컴포넌트는 오브젝트당 하나뿐이다.</summary>
    public List<T> GetComponents<T>() where T : Component
        => ScriptRegistry.FindAll<T>(Handle);

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

    /// <summary>범위를 벗어나면 무효 핸들을 담은 Entity가 나온다(IsAlive로 걸러진다).</summary>
    public Entity GetChild(int index) => new(Native.GetChild(Handle, index));

    /// <summary>부모. 최상위 오브젝트면 무효 핸들이다.</summary>
    public Entity Parent => new(Native.GetParent(Handle));

    /// <summary>씬 안에서의 인덱스. 엔진 자료구조와 대조할 때만 쓴다.</summary>
    public int Index => Native.GetIndex(Handle);

    /// <summary>인덱스로 찾는다. 엔진의 <c>Entity::FindIndex</c>에 해당한다.</summary>
    public static Entity FindByIndex(int index) => new(Native.FindByIndex(index));

    /// <summary>직계 자식을 순회한다. 순회 도중 자식이 바뀌면 결과가 어긋날 수 있다.</summary>
    public IEnumerable<Entity> Children
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

        static void Collect(Entity node, bool includeNode, List<T> into)
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

        Entity node = this;
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
    public static void Destroy(Entity target) => target.Destroy();

    /// <summary>이름으로 찾는다.</summary>
    public static Entity Find(string name) => new(Native.FindByName(name));

    public bool Equals(Entity other) => Handle.Equals(other.Handle);
    public override bool Equals(object? obj) => obj is Entity other && Equals(other);
    public override int GetHashCode() => Handle.GetHashCode();

    // 같은 오브젝트를 가리키는지 비교한다. 세대까지 보므로, 슬롯이 재사용된
    // 다른 오브젝트를 같다고 판정하지 않는다.
    public static bool operator ==(Entity a, Entity b) => a.Equals(b);
    public static bool operator !=(Entity a, Entity b) => !a.Equals(b);
    public override string ToString() => IsAlive ? $"Entity({Name})" : "Entity(<destroyed>)";
}
