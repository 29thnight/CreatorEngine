#pragma once
#include "Core.Minimal.h"
#include "TransCondition.generated.h"
#include "aniStruct.h"

class AnimationController;
class TransCondition
{
public:
   ReflectTransCondition
	[[Serializable]]
	TransCondition() = default;


	TransCondition(float Comparevalue, conditionType cType, valueType vType) : cType(cType) , CompareParameter(Comparevalue, vType)
	{
	

	}
	bool CheckTrans();
	//Ÿ�� ,�� ,�Լ� 
	[[Property]]
	std::string valueName{};
	[[Property]]
	conditionType cType = conditionType::Equal;
	[[Property]]
	aniParameter CompareParameter;

	AnimationController* ownerFSM{};
};

