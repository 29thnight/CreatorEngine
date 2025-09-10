#include "BP001BodyAtack.h"
#include "pch.h"
#include "DebugLog.h"
NodeStatus BP001BodyAtack::Tick(float deltatime, BlackBoard& blackBoard)
{
	LOG("BP001BodyAtack call");

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
		LOG("attack action running");
		return NodeStatus::Running;
	}
	

	
	//
	LOG("attack action Success end");
	blackBoard.SetValueAsFloat("IdleTime", 5.0f); //body atteck is add to IdleTime 5secend
	return NodeStatus::Success;
}
