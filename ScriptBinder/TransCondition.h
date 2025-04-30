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
	//타입 ,값 ,함수 
	[[Property]]
	std::string valueName{};
	[[Property]]
	conditionType cType = conditionType::Equal;
	[[Property]]
	aniParameter CompareParameter;

	AnimationController* ownerFSM{};
};

