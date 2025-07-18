#include "Idle.h"
#include "pch.h"

NodeStatus Idle::Tick(float deltatime, BlackBoard& blackBoard)
{
	// Perform idle behavior, such as waiting or doing nothing
	// This is a placeholder for actual idle logic
	
	// Return success to indicate that the idle action completed successfully
	// In a real implementation, you might check conditions or perform actions here
	bool isAnime = blackBoard.HasKey("AnimeState");
	if (isAnime)
	{
		std::string state = blackBoard.GetValueAsString("AnimeState");
		if (state == "Idle")
		{
			std::cout << "Idle action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in idle state
		}
		else
		{
			blackBoard.SetValueAsString("AnimeState", "Idle");
			std::cout << "Switching to Idle state." << std::endl;
		}
	}
	else
	{
		blackBoard.SetValueAsString("AnimeState", "Idle");
		std::cout << "Setting Idle state for the first time." << std::endl;
	}

	std::cout << "Idle action executed." << std::endl;

	return NodeStatus::Success;
}
