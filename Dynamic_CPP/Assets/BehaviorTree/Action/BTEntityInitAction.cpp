#include "BTEntityInitAction.h"
#include "pch.h"

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

	
	blackBoard.SetValueAsBool("Initialized", true);
	std::cout << "Behavior Tree Entity Initialized : "<< m_owner->GetHashedName().ToString() << std::endl;
	return NodeStatus::Success;
}
