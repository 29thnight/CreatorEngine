#include "RetreatAction.h"
#include "pch.h"
#include "EntityEleteMonster.h"
#include "DebugLog.h"
NodeStatus RetreatAction::Tick(float deltatime, BlackBoard& blackBoard)
{

	//컨디션에서 검사한거는 건너뛰자 문제되면 터지고 보자 
	bool hasState = blackBoard.HasKey("State");
	std::string State = "";
	if (hasState) {
		State = blackBoard.GetValueAsString("State");
	}


	EntityEleteMonster* script = m_owner->GetComponent< EntityEleteMonster>();

	//도망치는데 얼마나 도망치지 범위에 들어오자 마자 도망쳐서 
	//언제 도망치지 않고 싸우나? 아니면 틱마다 범위 들어오면 후퇴범위에 겹친만큼 이동?
	//일단 겹친만큼 이동하고 기회에 물어보자 

	if (State == "Retreat")
	{
		if (script->m_isRetreat) {
			// 도망 중일 때
			// 도망 로직을 구현해야 합니다.
			// 에니메이션이 재생 되는 상황에서 특정 타이밍에 맞춰서 도망가는 방향으로 이동하는 로직을 구현해야 합니다.
			LOG("Mage Retreat: Retreating to a safe location");
			return NodeStatus::Running;
		}
		else 
		{
			// 도망 후 쿨타임 설정
			State = "Idle"; //대기로 초기화
			blackBoard.SetValueAsString("State", State); // 상태를 블랙보드에 저장
			float cooldown = script->m_retreatCoolTime;
			blackBoard.SetValueAsFloat("ReteatCooldown",cooldown); // 쿨타임 설정
			return NodeStatus::Success;
		}
	}
	else {
		State = "Retreat"; // 상태를 도망으로 설정
		blackBoard.SetValueAsString("State", State); // 상태를 블랙보드에 저장
		script->StartRetreat();
		return NodeStatus::Running;
	}

	return NodeStatus::Success;
}
