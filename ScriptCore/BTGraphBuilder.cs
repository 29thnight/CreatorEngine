using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 저작 그래프의 노드 하나. 네이티브 ClrHost::ScriptBTNodeDesc와 배치가 같아야 한다.
///
/// 이름이 고정 길이 배열인 이유는 <see cref="ScriptMessage"/>와 같다 — 포인터가 섞이면
/// 배열 하나로 넘길 수 없고, 노드마다 경계를 넘게 되어 '틱당 1회' 규약이 무너진다.
/// 그래프는 트리를 만들 때 한 번만 건너오므로 복사 비용은 문제가 되지 않는다.
///
/// GUID는 <c>ulong</c>로 넘긴다. 네이티브 HashedGuid가 size_t 하나를 담는 구조라
/// 그대로 대응한다 — 부모·자식 관계를 이 값으로만 잇는다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BTNodeDesc
{
    public const int NameCapacity = 64;

    public ulong Id;
    public ulong ParentId;
    public int   Type;          // BehaviorNodeType
    public int   Policy;        // ParallelPolicy
    public int   IsRoot;        // 0/1 — bool은 배치가 처리계마다 흔들려 int로 고정한다
    public int   HasScript;     // 0/1
    public float Weight;        // 부모가 WeightedSelector일 때 이 자식의 가중치
    public int   ChildOrder;    // 부모의 자식 목록에서의 자리. 순서가 곧 실행 순서다

    public fixed byte Name[NameCapacity];
    public fixed byte ScriptName[NameCapacity];

    /// <summary>고정 길이 UTF-8 배열에서 문자열을 읽는다. NUL 이전까지가 내용이다.</summary>
    public static string ReadUtf8(byte* buffer, int capacity)
    {
        int length = 0;
        while (length < capacity && buffer[length] != 0) ++length;
        return length == 0 ? string.Empty
                           : System.Text.Encoding.UTF8.GetString(buffer, length);
    }
}


/// <summary>
/// 저작된 블랙보드 항목 하나. 네이티브 ClrHost::ScriptBBEntry와 배치가 같아야 한다.
///
/// 트리를 만들 때 노드 배열과 함께 한 번만 건너온다. 저작 값이 관리 측 블랙보드의
/// 초기 상태가 되어야 기존 BT 에셋이 같은 행동을 한다 — 비워 두면 노드가 읽는 값이
/// 전부 기본값이 되고, 그 차이는 크래시가 아니라 "AI가 좀 이상하다"로 나타난다.
///
/// 값 종류가 여덟인데 공용체를 쓰지 않은 이유는 배치 대조가 어려워지기 때문이다.
/// 항목 수가 수십 규모라 낭비가 문제되지 않는다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BBEntry
{
    public const int KeyCapacity = 64;
    public const int StringCapacity = 128;

    public int   Type;      // BlackBoardType
    public int   BoolValue; // 0/1
    public int   IntValue;
    public float FloatValue;
    public float X, Y, Z, W;

    public fixed byte Key[KeyCapacity];
    public fixed byte StringValue[StringCapacity];
}

/// <summary>한 트리의 틱 요청. 네이티브 ClrHost::ScriptAITick과 배치가 같아야 한다.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct AITick
{
    public int   InstanceId;
    public float DeltaTime;
}

/// <summary>
/// 평평한 노드 배열을 트리로 조립한다 (BehaviorTreeManagedPlan B3).
///
/// ── 왜 평평한 배열인가 ──
///
/// 네이티브의 BTBuildGraph는 포인터 맵(unordered_map&lt;HashedGuid, BTBuildNode*&gt;)이라
/// 그대로는 경계를 넘길 수 없다. 노드마다 넘기면 크로싱이 노드 수만큼 생기는데,
/// 그것이 정확히 이 재설계가 없애려던 것이다.
///
/// 그래서 저작 데이터의 형식은 건드리지 않고(기존 에셋이 그대로 열려야 한다),
/// 넘기는 순간에만 평평하게 편다. 부모·자식은 GUID로 잇고, 조립은 여기서 한다.
/// </summary>
internal static class BTGraphBuilder
{
    /// <summary>
    /// 노드 배열로 트리를 세우고 루트를 돌려준다. 실패하면 null.
    /// </summary>
    /// <param name="owner">노드가 붙을 오브젝트. 모든 노드가 같은 소유자를 본다.</param>
    public static unsafe BTNode? Build(BTNodeDesc* nodes, int count, GameObject owner, out string error)
    {
        error = string.Empty;

        if (nodes == null || count <= 0)
        {
            error = "노드가 비어 있다";
            return null;
        }

        // 1단계: 노드를 만든다. 아직 잇지 않는다.
        //
        // 자식이 부모보다 먼저 올 수 있으므로 두 번 돈다 — 저작 순서에 기대면
        // 에디터에서 노드를 옮겨 담는 순간 조용히 깨진다.
        var built = new Dictionary<ulong, BTNode>(count);
        var descById = new Dictionary<ulong, int>(count);
        ulong rootId = 0;

        for (int i = 0; i < count; ++i)
        {
            // 고정 크기 버퍼는 ref 지역 변수로 접근할 수 없다(CS1666) — 포인터로 직접 잡는다.
            BTNodeDesc* d = nodes + i;

            string name = BTNodeDesc.ReadUtf8(d->Name, BTNodeDesc.NameCapacity);
            string scriptName = BTNodeDesc.ReadUtf8(d->ScriptName, BTNodeDesc.NameCapacity);

            BTNode? node = CreateNode(d, scriptName, out string createError);
            if (node is null)
            {
                error = $"노드 '{name}' 생성 실패 — {createError}";
                return null;
            }

            node.Name = name;
            node.GameObject = owner;

            built[d->Id] = node;
            descById[d->Id] = i;

            if (d->IsRoot != 0) rootId = d->Id;
        }

        if (rootId == 0 || !built.ContainsKey(rootId))
        {
            error = "루트 노드가 없다";
            return null;
        }

        // 2단계: 부모 아래에 자식을 순서대로 단다.
        //
        // ChildOrder로 정렬하는 것이 핵심이다. Sequence·Selector는 자식 순서가 곧
        // 실행 순서라, 순서가 흔들리면 같은 그래프가 다른 행동을 한다.
        var childrenByParent = new Dictionary<ulong, List<int>>();
        for (int i = 0; i < count; ++i)
        {
            BTNodeDesc* d = nodes + i;
            if (d->ParentId == 0) continue;

            if (!childrenByParent.TryGetValue(d->ParentId, out var list))
            {
                list = new List<int>();
                childrenByParent[d->ParentId] = list;
            }
            list.Add(i);
        }

        foreach (var (parentId, childIndices) in childrenByParent)
        {
            if (!built.TryGetValue(parentId, out BTNode? parent))
            {
                error = $"부모 노드를 찾지 못했다 (id {parentId})";
                return null;
            }

            childIndices.Sort((a, b) => nodes[a].ChildOrder.CompareTo(nodes[b].ChildOrder));

            if (parent is CompositeNode composite)
            {
                var weights = new List<float>(childIndices.Count);
                foreach (int idx in childIndices)
                {
                    composite.AddChild(built[nodes[idx].Id]);
                    weights.Add(nodes[idx].Weight);
                }

                if (composite is WeightedSelectorNode weighted) weighted.SetWeights(weights);
            }
            else if (parent is DecoratorNode decorator)
            {
                // 데코레이터는 자식이 하나뿐이다. 저작 쪽에서도 막고 있지만
                // (BTBuildGraph::AddChildNode), 여기서 조용히 덮어쓰면 어느 자식이
                // 살아남았는지 알 수 없게 되므로 오류로 끝낸다.
                if (childIndices.Count > 1)
                {
                    error = $"데코레이터 '{decorator.Name}'에 자식이 {childIndices.Count}개다";
                    return null;
                }
                decorator.Child = built[nodes[childIndices[0]].Id];
            }
            else
            {
                error = $"'{parent.Name}'은(는) 자식을 가질 수 없는 노드인데 {childIndices.Count}개가 달려 있다";
                return null;
            }
        }

        return built[rootId];
    }

    private static unsafe BTNode? CreateNode(BTNodeDesc* desc, string scriptName, out string error)
    {
        error = string.Empty;

        // 스크립트 노드는 사용자가 C#으로 쓴 것이다. 이름으로 찾는다(B5의 등록표).
        if (desc->HasScript != 0)
        {
            if (string.IsNullOrEmpty(scriptName))
            {
                error = "스크립트 노드인데 이름이 비어 있다";
                return null;
            }

            BTNode? scripted = BTNodeFactory.Create(scriptName);
            if (scripted is null)
            {
                // 조용히 넘기지 않는다 — 등록을 빠뜨린 노드는 '아무 일도 안 하는 노드'가
                // 되어 AI가 미묘하게 다르게 움직이고, 그 원인은 짚기 어렵다.
                error = $"등록되지 않은 스크립트 노드 '{scriptName}'";
                return null;
            }
            return scripted;
        }

        var type = (BehaviorNodeType)desc->Type;
        switch (type)
        {
        case BehaviorNodeType.Sequence:         return new SequenceNode();
        case BehaviorNodeType.Selector:         return new SelectorNode();
        case BehaviorNodeType.WeightedSelector: return new WeightedSelectorNode();
        case BehaviorNodeType.Inverter:         return new InverterNode();

        case BehaviorNodeType.Parallel:
            // 정책은 생성자가 아니라 프로퍼티로 받는다(ParallelNode의 현재 형태).
            return new ParallelNode { Policy = (ParallelPolicy)desc->Policy };

        case BehaviorNodeType.Action:
        case BehaviorNodeType.Condition:
        case BehaviorNodeType.ConditionDecorator:
            // 이 셋은 본문이 사용자 코드다 — ConditionDecoratorNode도 ConditionCheck가
            // 추상이라 스스로 설 수 없다. 여기 왔다는 것은 저작 그래프에서 스크립트를
            // 지정하지 않았다는 뜻이고, 그대로 두면 '아무 일도 안 하는 노드'가 되어
            // AI가 미묘하게 다르게 움직인다. 오류로 끝낸다.
            error = $"{type} 노드는 스크립트를 지정해야 한다(HasScript=false)";
            return null;

        default:
            error = $"조립할 수 없는 노드 종류 {type}";
            return null;
        }
    }
}
