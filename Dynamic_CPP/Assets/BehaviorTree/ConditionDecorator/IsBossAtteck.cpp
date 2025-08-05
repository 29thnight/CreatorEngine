#include "IsBossAtteck.h"
#include "pch.h"

bool IsBossAtteck::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{

	bool isProcessed = blackBoard.HasKey("IdleTime");
	float idleTime; 
	if (isProcessed) {
		// atteack is add IdleTime to ActionNode
		idleTime = blackBoard.GetValueAsFloat("IdleTime");
		std::cout << "IdleTime is : " << idleTime << std::endl;
	}
	else 
	{
		// If IdleTime is not found, boss is not yet attacking
		idleTime = 0.0f; // Default value if not found
		std::cout << "IdleTime is not found, default to 0.0f" << std::endl;
	}

	if (idleTime > 0) {
		std::cout << "Boss is Idle, not attacking" << std::endl;
		return false; // Boss is Idle, not attacking
	}

	std::cout << "Boss is send attack node" << std::endl;
	return true; // Boss is attacking
}
