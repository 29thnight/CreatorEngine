#include "BossIdleAction.h"
#include "pch.h"
#include "DebugLog.h"
#include "TBoss1.h"
NodeStatus BossIdleAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//Idle is dufualt State

	//animation and logic to first
	bool hasIdentity = blackBoard.HasKey("Identity");
	std::string Identity = "";
	if (hasIdentity) {
		Identity = blackBoard.GetValueAsString("Identity");
	}

	bool isTime = blackBoard.HasKey("IdleTime");
	float duration;

	if (isTime) {
		duration = blackBoard.GetValueAsFloat("IdleTime");
		
		duration -= deltatime;

		if (duration < 0) {
			duration = 0.0f;
			blackBoard.SetValueAsFloat("IdleTime", duration);
			LOG("Boss Idle end to atteck");

			////test code  
			////Idle Time end demage add 5 as test phase2
			//float hp = blackBoard.GetValueAsInt("CurrHP");
			//hp -= 5;
			//LOG("Test Code excute Boss Demage 5");
			//blackBoard.SetValueAsInt("CurrHP",hp);
			////test code end
			if (Identity == "Boss1")
			{
				TBoss1* script = m_owner->GetComponent<TBoss1>();
				if (script) {
					script->actionCount = 0;
				}	
			}

			return NodeStatus::Success; //end Idle
		}

		blackBoard.SetValueAsFloat("IdleTime", duration);
		LOG("Boss Idle Running Remain Time : " << duration);
		return NodeStatus::Running;// running Idle
	}

	if (Identity == "Boss1")
	{
		TBoss1* script = m_owner->GetComponent<TBoss1>();
		if (script) {
			script->actionCount = 0;
		}
	}


	LOG("worning Idle set has none IdleTime");
	//time none set to all return success
	return NodeStatus::Success;
}
