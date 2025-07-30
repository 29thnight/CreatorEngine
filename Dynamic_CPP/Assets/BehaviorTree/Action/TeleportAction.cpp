#include "TeleportAction.h"
#include "pch.h"

NodeStatus TeleportAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//쿨타임 차서 사용 가능하고 적이 일정 범위 내에 있으므로 시작되는 로직
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "Mage")
	{
		return NodeStatus::Failure; // Identity가 없거나 "Mage"가 아닐 경우 실패 반환
	}

	auto State = blackBoard.GetValueAsString("State");

	if (State == "Teleport")
	{

		// 텔레포트 중일 때
		// 텔레포트 로직을 구현해야 합니다.
		// 에니메이션이 재생 되는 상황에서 특정 타이밍에 맟춰서 텔레포트 위치를 설정하고 이동하는 로직을 구현해야 합니다.

		std::cout << "Mage Teleport: Teleporting to a new location" << std::endl;
		// 텔레포트 후 쿨타임 설정
		float coolTime = blackBoard.GetValueAsFloat("eTPCooldown");
		blackBoard.SetValueAsFloat("TeleportCooldown", coolTime); // 텔레포트 쿨타임 설정
		
	}
	else {
		State = "Teleport"; // 상태를 텔레포트로 설정
		blackBoard.SetValueAsString("State", State); // 상태를 블랙보드에 저장
	}



	return NodeStatus::Success;
}
