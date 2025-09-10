#include "BTEntityInitAction.h"
#include "Animator.h"
#include "pch.h"
#include "DebugLog.h"
NodeStatus BTEntityInitAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//initialize the Behavior Tree Entity
	
	bool useDead = blackBoard.HasKey("HP");
	if (useDead)
	{
		//set the current HP to the initial HP
		int hp = blackBoard.GetValueAsInt("HP");
		blackBoard.SetValueAsInt("CurrHP", hp);
	}

	//todo : initialize other properties as needed

	std::string name = blackBoard.GetValueAsString("Identity");

	
	blackBoard.SetValueAsBool("Initialized", true);
	//LOG("Behavior Tree Entity Initialized : "<< m_owner->GetHashedName().ToString());
	return NodeStatus::Success;
}
