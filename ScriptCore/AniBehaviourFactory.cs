namespace CreatorEngine;

/// <summary>
/// 이름으로 <see cref="AniBehaviour"/>를 만들고 인스턴스를 들고 있는다.
///
/// <see cref="ScriptFactory"/>와 같은 구조지만 목록을 따로 둔다 — 두 계열은 수명이
/// 완전히 다르다. Behaviour는 오브젝트에 붙어 살고, AniBehaviour는 애니메이션 상태가
/// 만들고 지운다.
/// </summary>
internal static class AniBehaviourFactory
{
    private static readonly Dictionary<string, Func<AniBehaviour>> _creators = new();
    private static readonly Dictionary<int, AniBehaviour> _instances = new();

    private static int _nextInstanceId = 1;

    public static void Register(string typeName, Func<AniBehaviour> creator) => _creators[typeName] = creator;

    public static bool Exists(string typeName) => _creators.ContainsKey(typeName);

    public static int RegisteredCount => _creators.Count;

    /// <summary>
    /// 어셈블리를 내릴 때 등록과 인스턴스를 모두 비운다.
    /// 인스턴스를 빠뜨리면 컬렉터블 컨텍스트가 언로드되지 않는다(ScriptFactory에서 겪은 문제).
    /// </summary>
    public static void ClearRegistrations()
    {
        _creators.Clear();
        _instances.Clear();
    }

    public static int Create(string typeName)
    {
        if (!_creators.TryGetValue(typeName, out var factory))
        {
            Native.Log(3, $"[ScriptCore] 등록되지 않은 애니메이션 스크립트 타입: {typeName}");
            return -1;
        }

        int id = _nextInstanceId++;
        _instances[id] = factory();
        return id;
    }

    public static bool Destroy(int instanceId) => _instances.Remove(instanceId);

    public static AniBehaviour? Find(int instanceId)
        => _instances.TryGetValue(instanceId, out var behaviour) ? behaviour : null;

    /// <summary>
    /// 틱 경계에 모인 콜백을 순서대로 전달한다.
    ///
    /// 하나가 던진 예외로 프레임 전체가 죽지 않도록 각각 격리한다 —
    /// 다만 Behaviour와 달리 "이 스크립트만 끄기"가 없다. 애니메이션 상태 스크립트는
    /// 상태 머신이 소유하고 있어 여기서 끌 수 있는 스위치가 없기 때문이다.
    /// </summary>
    public static void Dispatch(in AniEvent evt)
    {
        AniBehaviour? behaviour = Find(evt.InstanceId);
        if (behaviour is null) return;

        // 소유 오브젝트는 이벤트마다 실려 온다. 상태가 만들어질 때는 아직
        // 컨트롤러가 연결되지 않아 생성 시점에 정할 수 없다.
        behaviour.GameObject = new GameObject(evt.Owner);
        behaviour.Transform = new Transform(evt.Owner);

        try
        {
            switch ((AniEventKind)evt.Kind)
            {
                case AniEventKind.Enter:  behaviour.Enter();               break;
                case AniEventKind.Update: behaviour.Update(evt.DeltaTime); break;
                case AniEventKind.Exit:   behaviour.Exit();                break;
            }
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[{behaviour.GetType().Name}] {(AniEventKind)evt.Kind} 예외\n{ex}");
        }
    }
}
