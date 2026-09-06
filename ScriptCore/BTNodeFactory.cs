namespace CreatorEngine;

/// <summary>사용자 BT 노드의 갈래. 생성기가 등록할 때 함께 넘긴다.</summary>
public enum BTNodeKind
{
    None = 0,
    Action = 1,
    Condition = 2,
    ConditionDecorator = 3,
}

/// <summary>
/// 이름으로 사용자 BT 노드를 만든다.
///
/// <see cref="ScriptFactory"/>·<see cref="AniBehaviorFactory"/>와 같은 구조다.
/// 목록을 따로 두는 이유도 같다 — 수명 주체가 다르고(트리가 소유한다),
/// 에디터가 갈래별로 목록을 보여 줘야 한다.
/// </summary>
internal static class BTNodeFactory
{
    private readonly record struct Entry(BTNodeKind Kind, Func<BTNode> Create);

    private static readonly Dictionary<string, Entry> _creators = new(StringComparer.Ordinal);

    public static void Register(string typeName, int kind, Func<BTNode> factory)
        => _creators[typeName] = new Entry((BTNodeKind)kind, factory);

    public static bool Exists(string typeName) => _creators.ContainsKey(typeName);

    public static int RegisteredCount => _creators.Count;

    /// <summary>
    /// 어셈블리를 내릴 때 비운다. 델리게이트가 스크립트 타입을 가리키고 있어
    /// 남겨 두면 컬렉터블 컨텍스트가 언로드되지 않는다(ScriptFactory에서 겪은 문제).
    /// </summary>
    public static void ClearRegistrations() => _creators.Clear();

    /// <summary>노드를 만든다. 등록되지 않은 이름이면 null.</summary>
    public static BTNode? Create(string typeName)
    {
        if (!_creators.TryGetValue(typeName, out Entry entry))
        {
            Native.Log(3, $"[ScriptCore] 등록되지 않은 BT 노드 타입: {typeName}");
            return null;
        }

        BTNode node = entry.Create();
        node.Name = typeName;
        return node;
    }

    /// <summary>갈래별 타입 이름. kind가 None이면 전부 돌려준다.</summary>
    public static IEnumerable<string> TypeNames(BTNodeKind kind)
        => kind == BTNodeKind.None
            ? _creators.Keys
            : _creators.Where(p => p.Value.Kind == kind).Select(p => p.Key);
}
