namespace CreatorEngine;

/// <summary>
/// 살아 있는 행동 트리 인스턴스를 든다 (BehaviorTreeManagedPlan B3·B4).
///
/// ── 왜 관리 측이 트리를 소유하는가 ──
///
/// BT는 <c>Running</c>을 프레임 간에 이어 간다(Sequence의 현재 자식, Parallel의 진행
/// 상태). 그 상태가 트리 노드 안에 있으므로, 트리가 관리 측에 있으면 상태도 여기 있어야
/// 한다. 네이티브는 인스턴스 id만 들고 "이걸 틱하라"만 말한다.
///
/// <see cref="ScriptFactory"/>와 구조가 같다. 목록을 따로 두는 이유는 수명 주체가
/// 다르기 때문이다 — 스크립트는 컴포넌트가, 트리는 BehaviorTreeComponent가 쥔다.
/// </summary>
internal static class BehaviorTreeRegistry
{
    private sealed class Instance(BTNode root, GameObject owner)
    {
        public readonly BTNode Root = root;
        public readonly GameObject Owner = owner;
        public BlackBoard BlackBoard = new();
    }

    private static readonly Dictionary<int, Instance> _trees = new();
    private static int _nextId = 1;

    // 진단 누계. 틱 경로에 있으므로 세는 것 말고는 아무 일도 하지 않는다.
    //
    // 트리 생성·틱은 성공해도 로그를 남기지 않는다(실패만 남긴다). 그래서 이 두 수가
    // 없으면 '트리가 안 서서 AI가 가만히 있다'와 '정상'이 밖에서 구분되지 않는다.
    private static long _tickCount;
    private static long _skippedCount;

    public static int Count => _trees.Count;

    /// <summary>진단 지표를 채운다. bt.status가 읽는다.</summary>
    public static BTStats GetStats() => new()
    {
        TreeCount     = _trees.Count,
        NodeTypeCount = BTNodeFactory.RegisteredCount,
        TickCount     = _tickCount,
        SkippedCount  = _skippedCount,
    };

    /// <summary>누계만 0으로 되돌린다. 트리는 건드리지 않는다.</summary>
    public static void ResetStats()
    {
        _tickCount = 0;
        _skippedCount = 0;
    }

    /// <summary>트리를 만들어 등록한다. 성공하면 0 이상의 id, 실패하면 음수.</summary>
    public static unsafe int Create(ObjectHandle owner, BTNodeDesc* nodes, int count,
        BBEntry* entries, int entryCount)
    {
        var gameObject = new GameObject(owner);

        BTNode? root = BTGraphBuilder.Build(nodes, count, gameObject, out string error);
        if (root is null)
        {
            // 조용히 넘기지 않는다. 트리가 안 서면 그 AI는 아무것도 하지 않는데,
            // 그 모습이 '가만히 있는 캐릭터'라 원인을 짚기 어렵다.
            Native.Log(3, $"[BT] 트리 조립 실패 — {error}");
            return -1;
        }

        int id = _nextId++;
        var instance = new Instance(root, gameObject);
        LoadBlackBoard(instance.BlackBoard, entries, entryCount);
        _trees[id] = instance;
        return id;
    }

    /// <summary>
    /// 저작된 값을 블랙보드의 초기 상태로 채운다.
    ///
    /// 이걸 빠뜨리면 노드가 읽는 값이 전부 기본값이 되어 기존 BT 에셋이 다르게
    /// 움직인다 — 크래시가 아니라 "AI가 좀 이상하다"로만 나타나는 종류다.
    /// </summary>
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
            case BlackBoardType.GameObject:
            case BlackBoardType.Transform:
                // GameObject·Transform은 저작 시점에 이름·경로 문자열로 남는다.
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

    public static bool Destroy(int instanceId) => _trees.Remove(instanceId);

    /// <summary>한 트리를 틱한다. 등록되지 않은 id면 아무 일도 하지 않는다.</summary>
    public static void Tick(int instanceId, float deltaTime)
    {
        if (!_trees.TryGetValue(instanceId, out Instance? tree)) { _skippedCount++; return; }

        // 소유자가 사라졌으면 틱하지 않는다. 트리는 다음 Destroy까지 남지만,
        // 그 사이에 죽은 핸들로 네이티브를 부르는 일은 없어야 한다.
        if (!tree.Owner.IsAlive) { _skippedCount++; return; }

        _tickCount++;
        tree.Root.Tick(deltaTime, tree.BlackBoard);
    }

    public static BlackBoard? GetBlackBoard(int instanceId)
        => _trees.TryGetValue(instanceId, out Instance? tree) ? tree.BlackBoard : null;

    /// <summary>
    /// 소유자가 사라진 트리를 거둔다. 거둔 수를 돌려준다.
    ///
    /// 씬 언로드에서 <see cref="Clear"/>를 부르면 안 되는 이유는 Behaviour 쪽과 같다 —
    /// DontDestroyOnLoad 오브젝트의 트리까지 없어진다. 소유자 생존으로 가른다.
    ///
    /// 정상 경로는 BehaviorTreeComponent::OnDestroy → DestroyBehaviorTree이고, 그쪽을
    /// 탄 트리는 이미 목록에서 빠져 있다. 즉 여기 걸리는 것은 <b>그 경로를 타지 못한
    /// 트리</b>뿐이라, 하나라도 나오면 수명 배선에 구멍이 있다는 신호다.
    /// </summary>
    public static int SweepOrphans()
    {
        List<int>? orphans = null;

        foreach (var (id, tree) in _trees)
        {
            if (tree.Owner.IsAlive) continue;
            (orphans ??= new()).Add(id);
        }

        if (orphans is null) return 0;

        foreach (int id in orphans) _trees.Remove(id);
        return orphans.Count;
    }

    /// <summary>
    /// 전부 비운다. <b>어셈블리 리로드 전용이다.</b>
    ///
    /// 트리 노드가 스크립트 타입(사용자 Action/Condition)을 가리키므로, 남겨 두면
    /// 컬렉터블 컨텍스트가 언로드되지 않는다 — ScriptFactory에서 겪은 그 문제다.
    ///
    /// 씬 언로드에는 쓰지 않는다(위 <see cref="SweepOrphans"/> 참고).
    /// </summary>
    public static void Clear()
    {
        _trees.Clear();
        _nextId = 1;
    }
}
