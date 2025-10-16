#include "BP0011.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0011::Tick(float deltatime, BlackBoard& blackBoard)
{
	
    TBoss1* script = m_owner->GetComponent<TBoss1>();
    if (script->GetActivePattern() != TBoss1::EPatternType::BP0011)
    {
        // 안전장치: 만약 다른 패턴이 이미 실행 중이라면, 이 노드는 실패 처리합니다.
        if (script->GetActivePattern() != TBoss1::EPatternType::None)
        {
            return NodeStatus::Failure;
        }

        // 보스에게 BP0034 패턴 시작을 '요청'합니다.
        script->BP0011();
    }

    // --- 상태 감시 역할 ---
    // TBoss1 객체는 자신의 Update() 함수에서 스스로 패턴을 진행시킵니다.
    // 이 노드는 보스의 현재 상태(m_activePattern)를 확인하기만 하면 됩니다.
    if (script->GetActivePattern() == TBoss1::EPatternType::None)
    {
        // 보스가 스스로 패턴을 끝낸 것을 확인했습니다.
        // 이 노드의 임무가 완수되었으므로 Success를 반환합니다.
        return NodeStatus::Success;
    }
    else
    {
        // 아직 보스가 BP0034 패턴을 진행 중인 것을 확인했습니다.
        // 다음 틱에서 다시 상태를 확인할 수 있도록 Running을 반환합니다.
        return NodeStatus::Running;
    }
}
