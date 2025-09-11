#include "TestAction.h"
#include "pch.h"
#include "DebugLog.h"
NodeStatus TestAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	LOG("TestAction Tick called with deltatime: " << deltatime);
	bool condition = blackBoard.HasKey("Cond1");
	int a = 0;
	if (condition) {
		a = blackBoard.GetValueAsInt("Cond1");
	}
	
	LOG("TestAction: a incremented to " << a);

	blackBoard.SetValueAsInt("Cond1", ++a); // Increment the value of "Cond1" by 1
	

	return NodeStatus::Success;
}
