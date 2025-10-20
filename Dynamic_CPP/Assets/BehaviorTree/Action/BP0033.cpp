#include "BP0033.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0033::Tick(float deltatime, BlackBoard& blackBoard)
{
    const auto MY_PATTERN_TYPE = TBoss1::EPatternType::BP0033; // 이 노드에 해당하는 패턴 타입
    TBoss1* script = m_owner->GetComponent<TBoss1>();

    // 1. 가장 먼저 '패턴이 방금 끝났는지' 확인
    if (script->GetLastCompletedPattern() == MY_PATTERN_TYPE)
    {
        script->ConsumeLastCompletedPattern(); // 상태를 '소비'하여 다음 Tick에서 재진입 방지
        script->hazardTimer = 0.0f; // 해저드 타이머 초기화
        return NodeStatus::Success; // 최종 성공 처리
    }

    const auto currentPattern = script->GetActivePattern();

    // 2. 보스가 아무것도 안 하고 있을 때 -> 패턴 시작
    if (currentPattern == TBoss1::EPatternType::None)
    {
        script->BP0033(); // 이 노드에 맞는 패턴 시작 함수 호출
        return NodeStatus::Running;
    }
    // 3. 우리 패턴이 실행 중일 때 -> 계속 Running
    else if (currentPattern == MY_PATTERN_TYPE)
    {
        return NodeStatus::Running;
    }
    // 4. 다른 패턴이 실행 중일 때 -> 실패 처리
    else
    {
        return NodeStatus::Failure;
    }
}
