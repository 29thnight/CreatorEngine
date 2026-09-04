namespace CreatorEngine;

/// <summary>
/// 타입 이름 → 스크립트 인스턴스 생성.
///
/// <c>Activator.CreateInstance</c> 같은 런타임 리플렉션을 쓰지 않는다. Native AOT가
/// 사용처를 정적으로 볼 수 없는 타입을 트리밍하기 때문이다(설계 문서 06절).
/// 대신 이름 → 팩토리 델리게이트 표를 명시적으로 채운다.
///
/// 지금은 <see cref="RegisterGenerated"/>를 손으로 구현하지만, 이 자리는 소스 제너레이터가
/// 대신 채우게 될 곳이다. 사용자 코드는 그대로 두고 생성 방식만 바뀐다.
/// </summary>
internal static class ScriptFactory
{
    private static readonly Dictionary<string, Func<Component>> _creators = new(StringComparer.Ordinal);
    private static readonly Dictionary<int, Component> _instances = new();
    private static int _nextInstanceId = 1;

    public static void Initialize()
    {
        _creators.Clear();
        _instances.Clear();
        _nextInstanceId = 1;
    }

    /// <summary>스크립트 어셈블리의 생성기 산출물이 이 메서드로 자기 타입들을 등록한다.</summary>
    public static void Register(string typeName, Func<Component> factory)
        => _creators[typeName] = factory;

    /// <summary>
    /// 팩토리 표와 인스턴스 목록을 모두 비운다. 리로드 전에 반드시 불러야 한다.
    ///
    /// 둘 다 비워야 한다 — 팩토리 델리게이트는 스크립트 타입을 가리키고,
    /// 인스턴스 딕셔너리는 객체 자체를 붙들고 있다. 어느 한쪽만 남아도
    /// 어셈블리 컨텍스트가 언로드되지 않는다(실제로 인스턴스 쪽을 빠뜨려 언로드가 막혔다).
    /// </summary>
    public static void ClearRegistrations()
    {
        _creators.Clear();
        _instances.Clear();
    }

    /// <summary>현재 등록된 스크립트 타입 수(진단용).</summary>
    public static int RegisteredCount => _creators.Count;

    /// <summary>등록된 스크립트 타입 이름들 — 에디터의 스크립트 추가 메뉴가 쓴다.</summary>
    public static IReadOnlyCollection<string> RegisteredTypeNames => _creators.Keys;

    public static int Create(ObjectHandle owner, string typeName)
    {
        if (!_creators.TryGetValue(typeName, out var factory))
        {
            Native.Log(3, $"[ScriptCore] 등록되지 않은 스크립트 타입: {typeName}");
            return -1;
        }

        Component b = factory();
        b.Entity = new Entity(owner);

        // Transform이 없는 오브젝트가 있다(UI/Canvas — S3). 여기서 한 번 갈라 두면
        // 없는 Transform에 값을 쓰는 일이 조용히 넘어가지 않는다(Component.Transform 주석).
        b.BindTransform(Native.HasTransform(owner) ? new Transform { OwnerHandle = owner } : null);

        int id = _nextInstanceId++;

        // id를 등록보다 **먼저** 인스턴스에 심는다. 스크립트가 OnInitialized에서
        // 곧바로 Enabled를 만질 수 있는데, 그 setter가 이 값으로 네이티브
        // ScriptComponent를 찾는다(Component.Enabled). 0이면 폴백 경고가 뜬다.
        b.InstanceId = id;

        _instances[id] = b;
        ScriptRegistry.Add(b);
        return id;
    }

    public static bool Destroy(int instanceId)
    {
        if (!_instances.Remove(instanceId, out var b)) return false;
        ScriptRegistry.Remove(b);
        return true;
    }

    /// <summary>인스펙터·직렬화가 인스턴스를 집어 오는 통로.</summary>
    public static Component? Find(int instanceId)
        => _instances.TryGetValue(instanceId, out var b) ? b : null;
}
