#include "DamegeAction.h"
#include "pch.h"

NodeStatus DamegeAction::Tick(float deltatime, BlackBoard& blackBoard)
{

	bool isDelayDamageAction = blackBoard.HasKey("DelayDamageAction");
	float delayDamageTime = 0.0f;
	if (isDelayDamageAction)
	{
		delayDamageTime = blackBoard.GetValueAsFloat("DelayDamageAction");
		delayDamageTime -= deltatime;
		blackBoard.SetValueAsFloat("DelayDamageAction", delayDamageTime);
	}
	else {
		std::cout << "No delay damage action found." << std::endl;
		blackBoard.SetValueAsFloat("DelayDamageAction", 2.0f);
	}

	std::cout << "Damege action executed successfully." << std::endl;
	return NodeStatus::Success;
}
