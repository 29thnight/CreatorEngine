#include "BossIdleAction.h"
#include "pch.h"
#include "DebugLog.h"
NodeStatus BossIdleAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//Idle is dufualt State

	//animation and logic to first


	bool isTime = blackBoard.HasKey("IdleTime");
	float duration;

	if (isTime) {
		duration = blackBoard.GetValueAsFloat("IdleTime");
		
		duration -= deltatime;

		if (duration < 0) {
			duration = 0.0f;
			blackBoard.SetValueAsFloat("IdleTime", duration);
			LOG("Boss Idle end to atteck");

			//test code  
			//Idle Time end demage add 5 as test phase2
			float hp = blackBoard.GetValueAsInt("CurrHP");
			hp -= 5;
			LOG("Test Code excute Boss Demage 5");
			blackBoard.SetValueAsInt("CurrHP",hp);
			//test code end

			return NodeStatus::Success; //end Idle
		}

		blackBoard.SetValueAsFloat("IdleTime", duration);
		LOG("Boss Idle Running Remain Time : " << duration);
		return NodeStatus::Running;// running Idle
	}

	LOG("worning Idle set has none IdleTime");
	//time none set to all return success
	return NodeStatus::Success;
}
