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
    public static void Register(string name, int kind, Func<BTNode> factory) => ManagedRuntime.Current.RegisterNode(name, kind, factory);
    public static bool Exists(string name) => ManagedRuntime.Current.NodeExists(name);
    public static int RegisteredCount => ManagedRuntime.Current.NodeCount;
    public static void ClearRegistrations() => ManagedRuntime.Current.ClearNodes();
    public static BTNode? Create(string name) => ManagedRuntime.Current.CreateNode(name);
    public static IEnumerable<string> TypeNames(BTNodeKind kind) => ManagedRuntime.Current.NodeNames(kind);
}
