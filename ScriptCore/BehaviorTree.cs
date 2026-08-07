namespace CreatorEngine;

/// <summary>노드 한 번 틱의 결과. 네이티브 NodeStatus와 값이 같아야 한다.</summary>
public enum NodeStatus
{
    Success = 0,
    Failure = 1,
    Aborted = 2,
    Running = 3,
}

/// <summary>저작 그래프의 노드 종류. 네이티브 BehaviorNodeType과 값이 같아야 한다.</summary>
public enum BehaviorNodeType
{
    Composite = 0,
    Decorator = 1,
    Sequence = 2,
    Selector = 3,
    WeightedSelector = 4,
    Inverter = 5,
    ConditionDecorator = 6,
    Condition = 7,
    Parallel = 8,
    Action = 9,
}

/// <summary>Parallel 노드의 성공 판정 정책. 네이티브 ParallelPolicy와 값이 같아야 한다.</summary>
public enum ParallelPolicy
{
    RequiredAll = 0,
    RequiredOne = 1,
}

/// <summary>
/// 행동 트리 노드의 기반.
///
/// 트리 전체가 관리 영역에 있으므로 순회는 경계를 넘지 않는다 — 네이티브는 틱 한 번만
/// 요청하고 그 뒤로는 여기서 끝난다(설계 문서 02절 · BehaviorTreeManagedPlan §1).
/// </summary>
public abstract class BTNode
{
    public string Name { get; internal set; } = string.Empty;

    /// <summary>노드가 붙은 오브젝트. 트리를 만들 때 컴포넌트 소유자로 채운다.</summary>
    public GameObject GameObject { get; internal set; }

    public abstract NodeStatus Tick(float deltaTime, BlackBoard blackBoard);
}

/// <summary>자식을 여럿 두는 노드.</summary>
public abstract class CompositeNode : BTNode
{
    private readonly List<BTNode> _children = new();

    public IReadOnlyList<BTNode> Children => _children;

    internal void AddChild(BTNode child) => _children.Add(child);

    /// <summary>파생 클래스가 인덱스로 순회할 수 있게 열어 둔다.</summary>
    protected List<BTNode> ChildList => _children;
}

/// <summary>자식을 하나만 두는 노드.</summary>
public abstract class DecoratorNode : BTNode
{
    public BTNode? Child { get; internal set; }
}

// ── 빌트인 노드 ──
// 네이티브 BTHeader.h의 동작을 그대로 옮긴다. 여기서 동작이 달라지면 기존 BT 에셋의
// 행동이 바뀌므로, 이식할 때 상태 보존(m_currentIndex 같은 것)까지 같아야 한다.

/// <summary>자식을 순서대로 실행하고 하나라도 실패하면 실패한다.</summary>
public sealed class SequenceNode : CompositeNode
{
    // 프레임을 넘겨 이어 간다 — Running으로 멈춘 자리에서 다시 시작한다.
    private int _currentIndex;

    public override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        while (_currentIndex < ChildList.Count)
        {
            NodeStatus status = ChildList[_currentIndex].Tick(deltaTime, blackBoard);
            if (status == NodeStatus.Running) return NodeStatus.Running;

            if (status == NodeStatus.Failure)
            {
                _currentIndex = 0;
                return NodeStatus.Failure;
            }
            ++_currentIndex;
        }

        _currentIndex = 0;
        return NodeStatus.Success;
    }
}

/// <summary>자식을 순서대로 시도하고 하나라도 성공하면 성공한다.</summary>
public sealed class SelectorNode : CompositeNode
{
    private int _currentIndex;

    public override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        while (_currentIndex < ChildList.Count)
        {
            NodeStatus status = ChildList[_currentIndex].Tick(deltaTime, blackBoard);
            if (status == NodeStatus.Running) return NodeStatus.Running;

            if (status == NodeStatus.Success)
            {
                _currentIndex = 0;
                return NodeStatus.Success;
            }
            ++_currentIndex;
        }

        _currentIndex = 0;
        return NodeStatus.Failure;
    }
}

/// <summary>가중치로 자식 하나를 뽑아 실행한다.</summary>
public sealed class WeightedSelectorNode : CompositeNode
{
    private readonly List<float> _weights = new();
    private int _runningChildIndex = -1;

    internal void SetWeights(IEnumerable<float> weights)
    {
        _weights.Clear();
        _weights.AddRange(weights);
    }

    public override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        // Running 중인 자식이 있으면 새로 뽑지 않고 이어 간다.
        if (_runningChildIndex >= 0 && _runningChildIndex < ChildList.Count)
        {
            NodeStatus running = ChildList[_runningChildIndex].Tick(deltaTime, blackBoard);
            if (running != NodeStatus.Running) _runningChildIndex = -1;
            return running;
        }

        if (ChildList.Count == 0) return NodeStatus.Failure;

        float total = 0f;
        for (int i = 0; i < _weights.Count; ++i) total += _weights[i];
        if (total <= 0f) return NodeStatus.Failure;

        float point = Random.Shared.NextSingle() * total;
        float sum = 0f;
        for (int i = 0; i < ChildList.Count && i < _weights.Count; ++i)
        {
            sum += _weights[i];
            if (point > sum) continue;

            NodeStatus status = ChildList[i].Tick(deltaTime, blackBoard);
            if (status == NodeStatus.Running) _runningChildIndex = i;
            return status;
        }

        return NodeStatus.Failure;
    }
}

/// <summary>자식을 모두 틱하고 정책에 따라 성패를 정한다.</summary>
public sealed class ParallelNode : CompositeNode
{
    public ParallelPolicy Policy { get; internal set; } = ParallelPolicy.RequiredAll;

    public override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        bool anyRunning = false;

        foreach (BTNode child in ChildList)
        {
            NodeStatus status = child.Tick(deltaTime, blackBoard);

            if (status == NodeStatus.Running) anyRunning = true;

            if (status == NodeStatus.Failure && Policy == ParallelPolicy.RequiredAll)
                return NodeStatus.Failure;

            if (status == NodeStatus.Success && Policy == ParallelPolicy.RequiredOne)
                return NodeStatus.Success;
        }

        if (Policy == ParallelPolicy.RequiredAll)
            return anyRunning ? NodeStatus.Running : NodeStatus.Success;

        return anyRunning ? NodeStatus.Running : NodeStatus.Failure;
    }
}

/// <summary>자식의 성패를 뒤집는다. Running은 그대로 통과시킨다.</summary>
public sealed class InverterNode : DecoratorNode
{
    public override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        if (Child is null) return NodeStatus.Failure;

        NodeStatus status = Child.Tick(deltaTime, blackBoard);
        return status switch
        {
            NodeStatus.Success => NodeStatus.Failure,
            NodeStatus.Failure => NodeStatus.Success,
            _ => status,
        };
    }
}

// ── 사용자 노드 ──
// 게임 코드가 상속해서 쓰는 두 종류. 생성기가 이름→생성 표를 만든다(ScriptFactory와 같은 구조).

/// <summary>행동을 수행하는 잎 노드. 여러 프레임에 걸치려면 Running을 돌려준다.</summary>
public abstract class ActionNode : BTNode
{
}

/// <summary>조건을 판정하는 잎 노드.</summary>
public abstract class ConditionNode : BTNode
{
    public sealed override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
        => ConditionCheck(deltaTime, blackBoard) ? NodeStatus.Success : NodeStatus.Failure;

    public abstract bool ConditionCheck(float deltaTime, BlackBoard blackBoard);
}

/// <summary>조건이 참일 때만 자식을 실행하는 데코레이터.</summary>
public abstract class ConditionDecoratorNode : DecoratorNode
{
    public sealed override NodeStatus Tick(float deltaTime, BlackBoard blackBoard)
    {
        if (Child is null) return NodeStatus.Failure;

        return ConditionCheck(deltaTime, blackBoard)
            ? Child.Tick(deltaTime, blackBoard)
            : NodeStatus.Failure;
    }

    public abstract bool ConditionCheck(float deltaTime, BlackBoard blackBoard);
}
