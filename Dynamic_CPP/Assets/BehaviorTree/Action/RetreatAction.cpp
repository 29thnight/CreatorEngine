#include "RetreatAction.h"
#include "pch.h"

NodeStatus RetreatAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//todo 도망 치기 어느 방향으로? 가장 가까운 플레이어와 반대로?
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "Mage")
	{
		return NodeStatus::Failure; // Identity가 없거나 "Mage"가 아닐 경우 실패 반환
	}
	auto State = blackBoard.GetValueAsString("State");
	if (State == "Retreat")
	{
		// 도망 중일 때
		// 도망 로직을 구현해야 합니다.
		// 에니메이션이 재생 되는 상황에서 특정 타이밍에 맞춰서 도망가는 방향으로 이동하는 로직을 구현해야 합니다.
		std::cout << "Mage Retreat: Retreating to a safe location" << std::endl;
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
