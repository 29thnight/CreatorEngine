#include "TestAction.h"
#include "pch.h"

NodeStatus TestAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	std::cout << "TestAction Tick called with deltatime: " << deltatime << std::endl;
	bool condition = blackBoard.HasKey("Cond1");
	int a = 0;
	if (condition) {
		a = blackBoard.GetValueAsInt("Cond1");
	}
	
	std::cout << "TestAction: a incremented to " << a << std::endl;

	blackBoard.SetValueAsInt("Cond1", ++a); // Increment the value of "Cond1" by 1
	

	return NodeStatus::Success;
}
