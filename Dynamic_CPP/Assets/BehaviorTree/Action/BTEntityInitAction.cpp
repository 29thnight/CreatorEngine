#include "BTEntityInitAction.h"
#include "Animator.h"
#include "pch.h"

NodeStatus BTEntityInitAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//initialize the Behavior Tree Entity
	
	bool useDead = blackBoard.HasKey("MaxHP");
	if (useDead)
	{
		//set the current HP to the initial HP
		int hp = blackBoard.GetValueAsInt("MaxHP");
		blackBoard.SetValueAsInt("CurrHP", hp);
	}

	//todo : initialize other properties as needed

	std::string name = blackBoard.GetValueAsString("Identity");

	
	blackBoard.SetValueAsBool("Initialized", true);
	std::cout << "Behavior Tree Entity Initialized : "<< m_owner->GetHashedName().ToString() << std::endl;
	return NodeStatus::Success;
}
