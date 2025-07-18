#include "AtteckAction.h"
#include "pch.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{

	// Example action: Print a message to the console
	std::cout << "AtteckAction executed!" << m_owner->GetHashedName().ToString() << std::endl;
	bool isAnime = blackBoard.HasKey("AnimeState");
	if (isAnime)
	{
		std::string state = blackBoard.GetValueAsString("AnimeState");
		if (state == "Atteck")
		{
			std::cout << "Atteck action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in attack state
		}
		else
		{
			blackBoard.SetValueAsString("AnimeState", "Atteck");
			std::cout << "Switching to Atteck state." << std::endl;
		}
	}

	return NodeStatus::Success;
}
