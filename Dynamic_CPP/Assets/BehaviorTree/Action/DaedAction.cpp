#include "DaedAction.h"
#include "pch.h"

NodeStatus DaedAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	// Example action: Print a message to the console
	
	bool isAnime = blackBoard.HasKey("AnimeState");
	if (isAnime)
	{
		std::string state = blackBoard.GetValueAsString("AnimeState");
		if (state == "Daed")
		{
			
			std::cout << "Daed action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in daed state
		}
		else
		{
			blackBoard.SetValueAsString("AnimeState", "Daed");
			std::cout << "Switching to Daed state." << std::endl;
		}
	}


	std::cout << "DaedAction executed!" << m_owner->GetHashedName().ToString() << std::endl;
	// You can access and modify the blackboard data here if needed
	// For example, you might want to set a value in the blackboard
	// blackBoard.SetValue("SomeKey", "SomeValue");
	// Return success or failure based on your action logic

	return NodeStatus::Success;
}
