#include "BossIdleAction.h"
#include "pch.h"

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
			std::cout << "Boss Idle end to atteck" << std::endl;
			//test code 
			//Idle Time end demage add 5 as test phase2
			float hp = blackBoard.GetValueAsInt("CurrHP");
			hp -= 5;
			std::cout << "Test Code excute Boss Demage 5" << std::endl;
			blackBoard.SetValueAsInt("CurrHP",hp);
			//test code end
			return NodeStatus::Success; //end Idle
		}

		blackBoard.SetValueAsFloat("IdleTime", duration);
		std::cout << "Boss Idle Running Remain Time : " << duration << std::endl;
		return NodeStatus::Running;// running Idle
	}

	std::cout << "worning Idle set has none IdleTime" << std::endl;
	//time none set to all return success
	return NodeStatus::Success;
}
