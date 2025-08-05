#include "GroggyAction.h"
#include "pch.h"

NodeStatus GroggyAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	float groggyTime = blackBoard.GetValueAsFloat("GroggyTime");
	
	std::cout << "GroggyAction Tick: Current GroggyTime: " << groggyTime << std::endl;

	//todo 
	//animation play or State Set 


	groggyTime -= deltatime; // Decrease groggy time by the delta time
	if (groggyTime <= 0.0f)
	{
		groggyTime = 0.0f; // Ensure groggy time does not go below zero
		blackBoard.SetValueAsFloat("GroggyTime", groggyTime); // Update the blackboard with the new groggy time
		return NodeStatus::Success; // Action performed successfully
	}
	else {
		blackBoard.SetValueAsFloat("GroggyTime", groggyTime); // Update the blackboard with the new groggy time
		return NodeStatus::Running; // Action is still running, groggy time is not yet over
	}
}
