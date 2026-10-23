namespace CreatorEngine;

// Parses native graph/blackboard values only. F# owns tree instances and execution.
internal static class BehaviorTreeRegistry
{
    public static int Count => ManagedRuntime.Current.TreeCount;
    public static BTStats GetStats() => ManagedRuntime.Current.GetTreeStats();
    public static void ResetStats() => ManagedRuntime.Current.ResetTreeStats();

    public static unsafe int Create(ObjectHandle owner, BTNodeDesc* nodes, int count, BBEntry* entries, int entryCount)
    {
        var entity = new Entity(owner);
        BTNode? root = BTGraphBuilder.Build(nodes, count, entity, out string error);
        if (root is null)
        {
            Native.Log(3, $"[BT] Graph assembly failed: {error}");
            return -1;
        }

        var board = new BlackBoard();
        LoadBlackBoard(board, entries, entryCount);
        return ManagedRuntime.Current.AddTree(root, entity, board);
    }

    private static unsafe void LoadBlackBoard(BlackBoard board, BBEntry* entries, int count)
    {
        if (entries == null || count <= 0) return;

        for (int i = 0; i < count; ++i)
        {
            // 고정 크기 버퍼는 ref 지역 변수로 접근할 수 없다(CS1666).
            BBEntry* e = entries + i;
            string key = BTNodeDesc.ReadUtf8(e->Key, BBEntry.KeyCapacity);
            if (key.Length == 0) continue;

            switch ((BlackBoardType)e->Type)
            {
            case BlackBoardType.Bool:    board.SetBool(key, e->BoolValue != 0); break;
            case BlackBoardType.Int:     board.SetInt(key, e->IntValue); break;
            case BlackBoardType.Float:   board.SetFloat(key, e->FloatValue); break;
            case BlackBoardType.Vector2: board.SetVector2(key, new Float2(e->X, e->Y)); break;
            case BlackBoardType.Vector3: board.SetVector3(key, new Float3(e->X, e->Y, e->Z)); break;

            case BlackBoardType.String:
            case BlackBoardType.Entity:
            case BlackBoardType.Transform:
                // Entity·Transform은 저작 시점에 이름·경로 문자열로 남는다.
                // 핸들로 푸는 것은 씬이 선 뒤에나 가능하므로 여기서는 문자열로 둔다 —
                // 노드가 필요할 때 이름으로 찾는다(네이티브 쪽도 같은 방식이었다).
                board.SetString(key, BTNodeDesc.ReadUtf8(e->StringValue, BBEntry.StringCapacity));
                break;

            default:
                // Vector4와 None은 관리 측 저장소에 대응이 없다. 조용히 버리지 않고 남긴다.
                Native.Log(2, $"[BT] 블랙보드 '{key}'의 타입 {(BlackBoardType)e->Type}은(는) 아직 옮기지 않았다");
                break;
            }
        }
    }

    public static bool Destroy(int instanceId) => ManagedRuntime.Current.DestroyTree(instanceId);
    public static void Tick(int instanceId, float dt) => ManagedRuntime.Current.TickTree(instanceId, dt);
    public static BlackBoard? GetBlackBoard(int instanceId) => ManagedRuntime.Current.GetTreeBlackBoard(instanceId);
    public static int SweepOrphans() => ManagedRuntime.Current.SweepTreeOrphans();
    public static void Clear() => ManagedRuntime.Current.ClearTrees();
}
