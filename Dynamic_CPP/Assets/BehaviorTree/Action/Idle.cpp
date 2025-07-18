#include "Idle.h"
#include "pch.h"

NodeStatus Idle::Tick(float deltatime, BlackBoard& blackBoard)
{
	// Perform idle behavior, such as waiting or doing nothing
	// This is a placeholder for actual idle logic
	
	// Return success to indicate that the idle action completed successfully
	// In a real implementation, you might check conditions or perform actions here

	std::cout << "Idle action executed." << std::endl;

	return NodeStatus::Success;
}
