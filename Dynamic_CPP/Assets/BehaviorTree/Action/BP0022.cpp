#include "BP0022.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0022::Tick(float deltatime, BlackBoard& blackBoard)
{
    TBoss1* script = m_owner->GetComponent<TBoss1>();
    if (script->GetActivePattern() != TBoss1::EPatternType::BP0022)
    {
        // ������ġ: ���� �ٸ� ������ �̹� ���� ���̶��, �� ���� ���� ó���մϴ�.
        if (script->GetActivePattern() != TBoss1::EPatternType::None)
        {
            return NodeStatus::Failure;
        }

        // �������� BP0022 ���� ������ '��û'�մϴ�.
        script->BP0022();
    }

    // --- ���� ���� ���� ---
    // TBoss1 ��ü�� �ڽ��� Update() �Լ����� ������ ������ �����ŵ�ϴ�.
    // �� ���� ������ ���� ����(m_activePattern)�� Ȯ���ϱ⸸ �ϸ� �˴ϴ�.
    if (script->GetActivePattern() == TBoss1::EPatternType::None)
    {
        // ������ ������ ������ ���� ���� Ȯ���߽��ϴ�.
        // �� ����� �ӹ��� �ϼ��Ǿ����Ƿ� Success�� ��ȯ�մϴ�.
        return NodeStatus::Success;
    }
    else
    {
        // ���� ������ BP0034 ������ ���� ���� ���� Ȯ���߽��ϴ�.
        // ���� ƽ���� �ٽ� ���¸� Ȯ���� �� �ֵ��� Running�� ��ȯ�մϴ�.
        return NodeStatus::Running;
    }
}
