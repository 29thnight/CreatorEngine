#include "RetreatAction.h"
#include "pch.h"
#include "DebugLog.h"
NodeStatus RetreatAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	auto State = blackBoard.GetValueAsString("State");
	if (State == "Retreat")
	{
		// 도망 중일 때
		// 도망 로직을 구현해야 합니다.
		// 에니메이션이 재생 되는 상황에서 특정 타이밍에 맞춰서 도망가는 방향으로 이동하는 로직을 구현해야 합니다.
		LOG("Mage Retreat: Retreating to a safe location");
		// 도망 후 쿨타임 설정
		float coolTime = blackBoard.GetValueAsFloat("eRetreatCooldown");
		blackBoard.SetValueAsFloat("RetreatCooldown", coolTime); // 도망 쿨타임 설정
	}
	else {
		State = "Retreat"; // 상태를 도망으로 설정
		blackBoard.SetValueAsString("State", State); // 상태를 블랙보드에 저장
	}



	return NodeStatus::Success;
}
