#include "BP001BodyAtack.h"
#include "pch.h"

NodeStatus BP001BodyAtack::Tick(float deltatime, BlackBoard& blackBoard)
{
	std::cout << "BP001BodyAtack call" << std::endl;

	float actionDuration = 5.0f;

	// Test code --> todo :: add action and animation Set
	bool isTimeCounter = blackBoard.HasKey("Counter");
	float time; 

	if (isTimeCounter) {
		time = blackBoard.GetValueAsFloat("Counters");
	}
	else {
		time = 0.0f;
	}

	// action 
	time += deltatime;
	

	if (time > actionDuration) {
		std::cout << "attack action running" << std::endl;
		return NodeStatus::Running;
	}
	

	
	//
	std::cout << "attack action Success end" << std::endl;
	blackBoard.SetValueAsFloat("IdleTime", 5.0f); //body atteck is add to IdleTime 5secend
	return NodeStatus::Success;
}
