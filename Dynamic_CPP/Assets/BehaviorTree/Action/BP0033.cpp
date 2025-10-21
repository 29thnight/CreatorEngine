#include "BP0033.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0033::Tick(float deltatime, BlackBoard& blackBoard)
{
    const auto MY_PATTERN_TYPE = TBoss1::EPatternType::BP0033; // �� ��忡 �ش��ϴ� ���� Ÿ��
    TBoss1* script = m_owner->GetComponent<TBoss1>();

    // 1. ���� ���� '������ ��� ��������' Ȯ��
    if (script->GetLastCompletedPattern() == MY_PATTERN_TYPE)
    {
        script->ConsumeLastCompletedPattern(); // ���¸� '�Һ�'�Ͽ� ���� Tick���� ������ ����
        script->hazardTimer = 0.0f; // ������ Ÿ�̸� �ʱ�ȭ
        return NodeStatus::Success; // ���� ���� ó��
    }

    const auto currentPattern = script->GetActivePattern();

    // 2. ������ �ƹ��͵� �� �ϰ� ���� �� -> ���� ����
    if (currentPattern == TBoss1::EPatternType::None)
    {
        script->BP0033(); // �� ��忡 �´� ���� ���� �Լ� ȣ��
        return NodeStatus::Running;
    }
    // 3. �츮 ������ ���� ���� �� -> ��� Running
    else if (currentPattern == MY_PATTERN_TYPE)
    {
        return NodeStatus::Running;
    }
    // 4. �ٸ� ������ ���� ���� �� -> ���� ó��
    else
    {
        return NodeStatus::Failure;
    }
}
