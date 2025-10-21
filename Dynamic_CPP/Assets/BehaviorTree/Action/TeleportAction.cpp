#include "TeleportAction.h"
#include "pch.h"
#include "EntityEleteMonster.h"
#include "DebugLog.h"
NodeStatus TeleportAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//쿨타임 차서 사용 가능하고 적이 일정 범위 내에 있으므로 시작되는 로직
	//컨디션에서 검사한거는 건너뛰자 문제되면 터지고 보자 
	bool hasState = blackBoard.HasKey("State");
	std::string State = "";
	if (hasState) {
		State = blackBoard.GetValueAsString("State");
	}


	EntityEleteMonster* script = m_owner->GetComponent< EntityEleteMonster>();
	bool action = script->m_isTeleport;

	//일단 에니메이션 처리 이펙트 처리 이런거 다 건너뛰고 일단 순간이동 시켜보자
	if (State == "Teleport") {
		if (action) {//행동 중이면 
			return NodeStatus::Running;
		}
		else //행동 끝난이후 대기 상태로 돌림
		{
			State = "Idle";
			blackBoard.SetValueAsString("State", State);
			script->m_isTeleport = false;
			script->m_posset = false;
			return NodeStatus::Success;
		}
	}
	else //첫 진입시 텔레포트 시작
	{
		State = "Teleport";
		blackBoard.SetValueAsString("State", State);
		if (script) {
			script->StartTeleport();
		}
		return NodeStatus::Running;
	}

	//if (State == "Teleport")
	//{

	//	// 텔레포트 중일 때
	//	// 텔레포트 로직을 구현해야 합니다.
	//	// 에니메이션이 재생 되는 상황에서 특정 타이밍에 맟춰서 텔레포트 위치를 설정하고 이동하는 로직을 구현해야 합니다.

	//	LOG("Mage Teleport: Teleporting to a new location");
	//	// 텔레포트 후 쿨타임 설정
	//	float coolTime = blackBoard.GetValueAsFloat("eTPCooldown");
	//	blackBoard.SetValueAsFloat("TeleportCooldown", coolTime); // 텔레포트 쿨타임 설정
	//	
	//}
	//else {
	//	State = "Teleport"; // 상태를 텔레포트로 설정
	//	blackBoard.SetValueAsString("State", State); // 상태를 블랙보드에 저장
	//}



	return NodeStatus::Success;
}
