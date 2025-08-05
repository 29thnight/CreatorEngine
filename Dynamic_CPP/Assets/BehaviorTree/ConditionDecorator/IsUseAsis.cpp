#include "IsUseAsis.h"
#include "pch.h"

bool IsUseAsis::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool isUseAsis = blackBoard.HasKey("eAsis");

	bool isAsisAction = false;

	if (isUseAsis)
	{
		isAsisAction = blackBoard.GetValueAsBool("eAsis");
	}
	else 
	{
		std::cout << "IsUseAsis: eAsis key does not exist in blackboard." << std::endl;
		return false;
	}

	GameObject* Target = nullptr;
	bool isTarget = blackBoard.HasKey("Target");
	if (isTarget) {
		Target = blackBoard.GetValueAsGameObject("Target");
		if (Target == nullptr) {
			std::cout << "IsUseAsis: Target is not set in blackboard." << std::endl;
			return false; // If Target is not set, return false
		}
	}
	else {
		std::cout << "IsUseAsis: Target key does not exist in blackboard." << std::endl;
		return false; // If Target key does not exist, return false
	}
	GameObject* Asis = nullptr;
	bool isAsis = blackBoard.HasKey("Asis");
	if (isAsis) {
		Asis = blackBoard.GetValueAsGameObject("Asis");
		if (Asis == nullptr) {
			std::cout << "IsUseAsis: Asis is not set in blackboard." << std::endl;
			return false; // If Asis is not set, return false
		}
	}
	else {
		std::cout << "IsUseAsis: Asis key does not exist in blackboard." << std::endl;
		return false; // If Asis key does not exist, return false
	}

	if (isAsisAction) 
	{
		if (Target==Asis)
		{
			std::cout << "IsUseAsis: Target is the same as Asis." << std::endl;
			return true;
		}
	}	

	std::cout << "Asis None Action or target not Asis" << std::endl;
	
	return false;
}
